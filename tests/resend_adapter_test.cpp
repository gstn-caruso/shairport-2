#include "session/session_state.hpp"
#include "protocol/rtp/rtp.h"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <thread>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include "cancellation_wait.hpp"

static CancellationWait *cancellation;
static std::atomic<bool> finishAtObservableCancellation{false};
static std::mutex captureMutex;
static std::condition_variable captured;
static unsigned captures = 0;
extern "C" bool __real__ZN17RemoteControlPeer15captureIfAbsentEPK8sockaddrj(RemoteControlPeer *,const sockaddr *,socklen_t);
extern "C" bool __wrap__ZN17RemoteControlPeer15captureIfAbsentEPK8sockaddrj(RemoteControlPeer *owner,const sockaddr *address,socklen_t length) {
  const bool result = __real__ZN17RemoteControlPeer15captureIfAbsentEPK8sockaddrj(owner,address,length);
  { std::lock_guard lock(captureMutex); ++captures; }
  captured.notify_all();
  return result;
}
extern "C" ssize_t __real_recvfrom(int,void *,size_t,int,sockaddr *,socklen_t *);
extern "C" ssize_t __wrap_recvfrom(int fd,void *bytes,size_t length,int flags,sockaddr *peer,socklen_t *peerLength) {
  if (finishAtObservableCancellation.load()) cancellation->block();
  return __real_recvfrom(fd,bytes,length,flags,peer,peerLength);
}

TEST(ResendRuntime, NativeControlCaptureAndResendPreserveExactWireAndCleanup) {
  CancellationWait waiting;
  cancellation = &waiting;
  const double fraction = config.diagnostic_drop_packet_fraction;
  config.diagnostic_drop_packet_fraction = 0;
  SessionState session{};
  session.rtp_running = 1;
  session.airplay_stream_type = buffered_stream;
  session.ap2_control_socket = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(session.ap2_control_socket,0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(session.ap2_control_socket,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  socklen_t size = sizeof(address);
  ASSERT_EQ(getsockname(session.ap2_control_socket,reinterpret_cast<sockaddr *>(&address),&size),0);
  const int client = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(client,0);
  ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,rtp_ap2_control_receiver,&session),0);
  std::array<uint8_t,28> control{};
  control[0] = 0x10;
  control[1] = 0xff;
  ASSERT_EQ(send(client,control.data(),control.size(),0),28);
  std::array<uint8_t,8> request{};
  bool arrived = false;
  const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(1);
  do {
    rtp_request_resend(0xcdef,0x12345,&session);
    pollfd pending{client,POLLIN,0};
    if (poll(&pending,1,0) == 1) {
      arrived = recv(client,request.data(),request.size(),0) == 8;
      break;
    }
    std::this_thread::yield();
  } while (std::chrono::steady_clock::now() < deadline);
  finishAtObservableCancellation.store(true);
  EXPECT_EQ(send(client,control.data(),control.size(),0),28);
  waiting.waitForBlocked(1);
  EXPECT_EQ(pthread_cancel(worker),0);
  EXPECT_EQ(pthread_join(worker,nullptr),0);
  finishAtObservableCancellation.store(false);
  close(client);
  config.diagnostic_drop_packet_fraction = fraction;
  ASSERT_TRUE(arrived);
  EXPECT_EQ(request,(std::array<uint8_t,8>{0x80,0xd5,0,1,0xcd,0xef,0x23,0x45}));
  EXPECT_EQ(session.ap2_control_socket,-1);
  EXPECT_FALSE(session.remoteControlPeer.snapshot());
}

TEST(ResendRuntime, D7AndD6KeepFirstPeerUntilCleanupThenAllowNewEndpoint) {
  CancellationWait waiting;
  cancellation = &waiting;
  { std::lock_guard lock(captureMutex); captures = 0; }
  SessionState session{};
  session.rtp_running = 1;
  session.airplay_stream_type = buffered_stream;
  const double fraction = config.diagnostic_drop_packet_fraction;
  config.diagnostic_drop_packet_fraction = 0;
  const auto bindReceiver = [&] {
    session.ap2_control_socket = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
    EXPECT_GE(session.ap2_control_socket,0);
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(session.ap2_control_socket,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
    socklen_t size = sizeof(address);
    EXPECT_EQ(getsockname(session.ap2_control_socket,reinterpret_cast<sockaddr *>(&address),&size),0);
    return address;
  };
  auto address = bindReceiver();
  const int first = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  const int second = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(first,0); ASSERT_GE(second,0);
  ASSERT_EQ(connect(first,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  ASSERT_EQ(connect(second,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,rtp_ap2_control_receiver,&session),0);
  std::array<uint8_t,28> packet{};
  packet[0] = 0x10; packet[1] = 0xd7;
  ASSERT_EQ(send(first,packet.data(),packet.size(),0),28);
  {
    std::unique_lock lock(captureMutex);
    ASSERT_TRUE(captured.wait_for(lock,std::chrono::seconds(1),[]{ return captures >= 1; }));
  }
  packet[1] = 0xd6;
  ASSERT_EQ(send(second,packet.data(),packet.size(),0),28);
  {
    std::unique_lock lock(captureMutex);
    ASSERT_TRUE(captured.wait_for(lock,std::chrono::seconds(1),[]{ return captures >= 2; }));
  }
  const auto receiveRequest = [&](int client) {
    rtp_request_resend(0xabcd,2,&session);
    pollfd pending{client,POLLIN,0};
    EXPECT_EQ(poll(&pending,1,500),1);
    std::array<uint8_t,8> request{};
    EXPECT_EQ(recv(client,request.data(),request.size(),MSG_DONTWAIT),8);
    EXPECT_EQ(request,(std::array<uint8_t,8>{0x80,0xd5,0,1,0xab,0xcd,0,2}));
  };
  receiveRequest(first);
  pollfd unexpected{second,POLLIN,0}; EXPECT_EQ(poll(&unexpected,1,0),0);
  const auto stopWorker = [&](int client,unsigned blocked) {
    finishAtObservableCancellation.store(true);
    EXPECT_EQ(send(client,packet.data(),packet.size(),0),28);
    waiting.waitForBlocked(blocked);
    EXPECT_EQ(pthread_cancel(worker),0); EXPECT_EQ(pthread_join(worker,nullptr),0);
    finishAtObservableCancellation.store(false);
    EXPECT_EQ(session.ap2_control_socket,-1);
    EXPECT_FALSE(session.remoteControlPeer.snapshot());
  };
  stopWorker(first,1);
  unsigned previousCaptures;
  { std::lock_guard lock(captureMutex); previousCaptures = captures; }
  address = bindReceiver();
  ASSERT_EQ(connect(second,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  ASSERT_EQ(pthread_create(&worker,nullptr,rtp_ap2_control_receiver,&session),0);
  packet[1] = 0xd7;
  ASSERT_EQ(send(second,packet.data(),packet.size(),0),28);
  {
    std::unique_lock lock(captureMutex);
    ASSERT_TRUE(captured.wait_for(lock,std::chrono::seconds(1),[previousCaptures]{ return captures > previousCaptures; }));
  }
  receiveRequest(second);
  stopWorker(second,2);
  close(first); close(second);
  config.diagnostic_drop_packet_fraction = fraction;
}

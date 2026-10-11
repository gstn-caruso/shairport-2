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
#include "cancellation_wait.hpp"

static CancellationWait cancellation;
static std::atomic<bool> finishAtObservableCancellation{false};
extern "C" ssize_t __real_recvfrom(int,void *,size_t,int,sockaddr *,socklen_t *);
extern "C" ssize_t __wrap_recvfrom(int fd,void *bytes,size_t length,int flags,sockaddr *peer,socklen_t *peerLength) {
  if (finishAtObservableCancellation.load()) cancellation.block();
  return __real_recvfrom(fd,bytes,length,flags,peer,peerLength);
}

TEST(ResendRuntime, NativeControlCaptureAndResendPreserveExactWireAndCleanup) {
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
  cancellation.waitForBlocked(1);
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

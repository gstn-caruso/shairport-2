#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/ap2/native_event_channel.hpp"
#include <gtest/gtest.h>
#include <array>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore>
#include <sys/wait.h>
#include <netinet/in.h>
#include <future>
#include <atomic>
#include "cancellation_wait.hpp"
#include "protocol/ap2/ap2_event_receiver.h"

static std::binary_semaphore updateInfoObserved{0};
static int observedReadFd = -1;
static std::atomic<bool> observeNativeRead{false};
static std::promise<void> nativeReading;
static CancellationWait encryptedReadCancellation;
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" ssize_t __wrap_read(int fd,void *bytes,size_t count) {
  if (fd == observedReadFd && observeNativeRead.exchange(false)) {
    nativeReading.set_value();
#if __has_feature(thread_sanitizer)
    encryptedReadCancellation.block();
#endif
  }
  return __real_read(fd,bytes,count);
}
extern "C" ssize_t __wrap_ap2_event_send_update_info(rtsp_conn_info *) {
  updateInfoObserved.release(); return 0;
}

static int receiverWithDescriptorZero() {
  const int listener = socket(AF_INET,SOCK_STREAM,0),client = socket(AF_INET,SOCK_STREAM,0);
  if (listener < 0 || client < 0) return 10;
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(listener,reinterpret_cast<sockaddr *>(&address),sizeof(address)) || listen(listener,1)) return 11;
  socklen_t length = sizeof(address);
  if (getsockname(listener,reinterpret_cast<sockaddr *>(&address),&length) ||
      connect(client,reinterpret_cast<sockaddr *>(&address),sizeof(address))) return 12;
  SessionState session{}; session.event_socket = listener;
  close(STDIN_FILENO);
  pthread_t worker;
  if (pthread_create(&worker,nullptr,ap2_event_receiver,&session)) return 13;
  const bool observed = updateInfoObserved.try_acquire_for(std::chrono::milliseconds(500));
  if (observed) pthread_cancel(worker);
  pthread_join(worker,nullptr);
  close(listener); close(client);
  return observed && fcntl(STDIN_FILENO,F_GETFD) == -1 ? 0 : 14;
}

TEST(EventCommandRuntime, ReceiverAttachesAcceptedDescriptorZeroAndCleanupClosesIt) {
  const pid_t child = fork(); ASSERT_GE(child,0);
  if (child == 0) _exit(receiverWithDescriptorZero());
  int status; ASSERT_EQ(waitpid(child,&status,0),child);
  ASSERT_TRUE(WIFEXITED(status)); EXPECT_EQ(WEXITSTATUS(status),0);
}

ssize_t ap2_event_port_post_command(rtsp_conn_info *,plist_t);
TEST(NativeEventChannel, CloseOwnsAcceptedFdWithoutAccessingReleasedCipher) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  pair_cipher_bundle cipher{};
  auto channel = makeNativeEventChannel(sockets[0],cipher);
  EXPECT_NE(fcntl(sockets[0],F_GETFD),-1);
  cipher.release(); channel.reset();
  EXPECT_EQ(fcntl(sockets[0],F_GETFD),-1); close(sockets[1]);
}

TEST(NativeEventChannel, RejectedAttachmentClosesOnlyCandidateDescriptor) {
  int first[2],second[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,first),0);
  ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,second),0);
  pair_cipher_bundle cipher{};
  EventCommandExchange exchange;
  ASSERT_TRUE(exchange.attach(makeNativeEventChannel(first[0],cipher)));
  EXPECT_FALSE(exchange.attach(makeNativeEventChannel(second[0],cipher)));
  EXPECT_EQ(fcntl(second[0],F_GETFD),-1); EXPECT_NE(fcntl(first[0],F_GETFD),-1);
  exchange.close(); EXPECT_EQ(fcntl(first[0],F_GETFD),-1);
  close(first[1]); close(second[1]);
}
TEST(EventCommandRuntime, BinaryPlistPostUsesRealEventCipherAcrossTwoTransactions) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  const timeval timeout{1,0};
  for (int fd : sockets) ASSERT_EQ(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),0);
  SessionState session{};
  pair_cipher_bundle peer{};
  const std::array<uint8_t,32> key{};
  auto &cipher = session.ap2_pairing_context.event_cipher_bundle;
  cipher.cipher_ctx = pair_cipher_new(PAIR_SERVER_HOMEKIT,4,key.data(),key.size(),"");
  peer.cipher_ctx = pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL,1,key.data(),key.size(),"");
  ASSERT_NE(cipher.cipher_ctx,nullptr); ASSERT_NE(peer.cipher_ctx,nullptr);
  ASSERT_TRUE(session.eventCommandExchange.attach(makeNativeEventChannel(sockets[0],cipher)));
  std::vector<std::vector<uint8_t>> received;
  const std::vector<std::vector<uint8_t>> replies{{'o',0,'k'},{'n','e','x','t'}};
  std::thread client([&] {
    for (const auto &reply : replies) {
      std::array<uint8_t,4096> bytes{};
      const auto count = read_encrypted(sockets[1],&peer,bytes.data(),bytes.size());
      EXPECT_GT(count,0); if (count <= 0) break;
      received.emplace_back(bytes.begin(),bytes.begin()+count);
      EXPECT_EQ(write_encrypted(sockets[1],&peer,reply.data(),reply.size()),static_cast<ssize_t>(reply.size()));
    }
  });
  const char data[]{0,1,static_cast<char>(0xff)};
  auto command = plist_new_data(data,sizeof(data));
  EXPECT_EQ(ap2_event_port_post_command(&session,command),3);
  EXPECT_EQ(ap2_event_port_post_command(&session,command),4);
  plist_free(command); client.join();
  const std::vector<uint8_t> binary{'b','p','l','i','s','t','0','0',0x43,0,1,0xff,8,
      0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,12};
  const std::string header = "POST /command RTSP/1.0\r\nContent-Length: 45\r\nContent-Type: application/x-apple-binary-plist\r\n\r\n";
  std::vector<uint8_t> expected(header.begin(),header.end()); expected.insert(expected.end(),binary.begin(),binary.end());
  ASSERT_EQ(received.size(),2u); EXPECT_EQ(received[0],expected); EXPECT_EQ(received[1],expected);
  session.eventCommandExchange.close(); close(sockets[1]);
  cipher.release(); peer.release();
}

TEST(EventCommandRuntime, CancellationDuringNativeEncryptedResponseUnlocksAndClosesBeforeCipherRelease) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  SessionState session{}; pair_cipher_bundle peer{};
  const std::array<uint8_t,32> key{};
  auto &cipher = session.ap2_pairing_context.event_cipher_bundle;
  cipher.cipher_ctx = pair_cipher_new(PAIR_SERVER_HOMEKIT,4,key.data(),key.size(),"");
  peer.cipher_ctx = pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL,1,key.data(),key.size(),"");
  ASSERT_NE(cipher.cipher_ctx,nullptr); ASSERT_NE(peer.cipher_ctx,nullptr);
  ASSERT_TRUE(session.eventCommandExchange.attach(makeNativeEventChannel(sockets[0],cipher)));
  auto reading = nativeReading.get_future(); observedReadFd = sockets[0]; observeNativeRead = true;
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,[](void *context)->void * {
    auto command = plist_new_string("ordinary command");
    std::unique_ptr<std::remove_pointer_t<plist_t>,decltype(&plist_free)> owned(command,plist_free);
    ap2_event_port_post_command(static_cast<SessionState *>(context),owned.get()); return nullptr;
  },&session),0);
  ASSERT_EQ(reading.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  std::array<uint8_t,4096> request{};
  EXPECT_GT(read_encrypted(sockets[1],&peer,request.data(),request.size()),0);
  EXPECT_EQ(pthread_cancel(worker),0); EXPECT_EQ(pthread_join(worker,nullptr),0);
  auto closing = std::async(std::launch::async,[&] { session.eventCommandExchange.close(); });
  ASSERT_EQ(closing.wait_for(std::chrono::milliseconds(500)),std::future_status::ready);
  closing.get(); EXPECT_EQ(fcntl(sockets[0],F_GETFD),-1);
  EXPECT_NE(cipher.cipher_ctx,nullptr);
  cipher.release(); peer.release(); close(sockets[1]); observedReadFd = -1;
}

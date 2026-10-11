#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/ap2/native_event_channel.hpp"
#include "protocol/ap2/native_encrypted_channel.hpp"
#include "platform/utilities/debug.h"
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
#include <csignal>
#include <gcrypt.h>
#include "cancellation_wait.hpp"
#include "protocol/ap2/ap2_event_receiver.h"

static std::binary_semaphore updateInfoObserved{0};
static int observedReadFd = -1;
static std::atomic<bool> observeNativeRead{false};
static std::promise<void> nativeReading;
static CancellationWait encryptedReadCancellation;
static std::atomic<int> observedWriteFd{-1};
static std::promise<void> nativeWriting;
static std::atomic<int> cooperativeReadFd{-1};
static std::promise<void> cooperativeReading;
static std::atomic<int> destructionReadFd{-1};
static std::promise<void> destructionReading;
static std::atomic<bool> observeDestructionCipherRelease{false};
static std::atomic<bool> destructionCallbackCompleted{false};
static std::atomic<int> destructionAcceptedFd{-1};
static int destructionCipherReleaseCount = 0;
static int destructionSetupReleaseCount = 0, destructionVerifyReleaseCount = 0;
extern "C" void __real_pair_setup_free(pair_setup_context *);
extern "C" void __real_pair_verify_free(pair_verify_context *);
static void requireCompletedEventLifetime() {
  EXPECT_TRUE(destructionCallbackCompleted.load());
  EXPECT_EQ(fcntl(destructionAcceptedFd.load(),F_GETFD),-1);
}
extern "C" void __wrap_pair_setup_free(pair_setup_context *context) {
  if (context && observeDestructionCipherRelease.load()) {
    requireCompletedEventLifetime(); ++destructionSetupReleaseCount;
  }
  __real_pair_setup_free(context);
}
extern "C" void __wrap_pair_verify_free(pair_verify_context *context) {
  if (context && observeDestructionCipherRelease.load()) {
    requireCompletedEventLifetime(); ++destructionVerifyReleaseCount;
  }
  __real_pair_verify_free(context);
}
extern "C" void __real_pair_cipher_free(pair_cipher_context *);
extern "C" void __wrap_pair_cipher_free(pair_cipher_context *cipher) {
  if (cipher && observeDestructionCipherRelease.load()) {
    requireCompletedEventLifetime();
    ++destructionCipherReleaseCount;
  }
  __real_pair_cipher_free(cipher);
}
extern "C" ssize_t __real_write(int,const void *,size_t);
extern "C" ssize_t __wrap_write(int fd,const void *bytes,size_t count) {
  int expected = fd;
  if (observedWriteFd.compare_exchange_strong(expected,-1)) nativeWriting.set_value();
  return __real_write(fd,bytes,count);
}
extern "C" ssize_t __real_read(int,void *,size_t);
extern "C" ssize_t __wrap_read(int fd,void *bytes,size_t count) {
  int expected = fd;
  if (cooperativeReadFd.compare_exchange_strong(expected,-1)) cooperativeReading.set_value();
  expected = fd;
  if (destructionReadFd.compare_exchange_strong(expected,-1)) destructionReading.set_value();
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
  if (bind(listener,reinterpret_cast<sockaddr *>(&address),sizeof(address))) return 11;
  socklen_t length = sizeof(address);
  if (getsockname(listener,reinterpret_cast<sockaddr *>(&address),&length)) return 12;
  SessionState session{};
  if (!start_ap2_event_worker(&session,listener)) return 13;
  close(STDIN_FILENO);
  if (connect(client,reinterpret_cast<sockaddr *>(&address),sizeof(address))) return 12;
  const bool observed = updateInfoObserved.try_acquire_for(std::chrono::milliseconds(500));
  stop_ap2_event_worker(&session);
  close(client);
  return observed && fcntl(STDIN_FILENO,F_GETFD) == -1 ? 0 : 14;
}

TEST(EventCommandRuntime, ReceiverAttachesAcceptedDescriptorZeroAndCleanupClosesIt) {
  const pid_t child = fork(); ASSERT_GE(child,0);
  if (child == 0) _exit(receiverWithDescriptorZero());
  int status; ASSERT_EQ(waitpid(child,&status,0),child);
  ASSERT_TRUE(WIFEXITED(status)); EXPECT_EQ(WEXITSTATUS(status),0);
}

ssize_t ap2_event_port_post_command(rtsp_conn_info *,plist_t);
class RuntimeDiagnosticReplyChannel final : public EventPlaintextChannel {
public:
  explicit RuntimeDiagnosticReplyChannel(bool embeddedNull) {
    reply_.fill('R'); if (embeddedNull) reply_[3] = 0;
  }
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override { return bytes.size(); }
  std::ptrdiff_t read(std::span<uint8_t> bytes) override {
    EXPECT_EQ(bytes.size(),reply_.size());
    std::copy(reply_.begin(),reply_.end(),bytes.begin()); return reply_.size();
  }
private:
  std::array<uint8_t,4096> reply_;
};

TEST(EventCommandRuntime, ActualReplyDiagnosticBoundsFullCapacityAndStopsAtFirstNull) {
  struct RestoreDebugLevel {
    int previous = debug_level();
    ~RestoreDebugLevel() { set_debug_level(previous); }
  } restore;
  set_debug_level(3);
  for (bool embeddedNull : {false,true}) {
    SessionState session{};
    ASSERT_TRUE(session.eventCommandExchange.attach(std::make_unique<RuntimeDiagnosticReplyChannel>(embeddedNull)));
    auto command = plist_new_string("ordinary command");
    testing::internal::CaptureStderr();
    const auto count = ap2_event_port_post_command(&session,command);
    const auto output = testing::internal::GetCapturedStderr();
    plist_free(command);
    EXPECT_EQ(count,4096);
    const std::string marker = "Packet Received on Event Port with contents: \n--\n";
    const auto start = output.find(marker);
    ASSERT_NE(start,std::string::npos);
    const auto body = start+marker.size();
    const auto end = output.find("\n--\n",body);
    ASSERT_NE(end,std::string::npos);
    EXPECT_EQ(output.substr(body,end-body),std::string(embeddedNull ? 3 : 4096,'R'));
  }
}

TEST(NativeEventChannel, CloseOwnsAcceptedFdWithoutAccessingReleasedCipher) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  EncryptedChannel cipher{};
  auto channel = makeNativeEventChannel(sockets[0],cipher);
  EXPECT_NE(fcntl(sockets[0],F_GETFD),-1);
  cipher.release(); channel.reset();
  EXPECT_EQ(fcntl(sockets[0],F_GETFD),-1); close(sockets[1]);
}

TEST(NativeEventChannel, InterruptPinsSocketAndWakesReadWithoutClosingChannel) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  EncryptedChannel cipher{};
  auto accepted = makeInterruptibleNativeEventChannel(sockets[0],cipher);
  ASSERT_TRUE(accepted);
  auto reading = std::async(std::launch::async,[&]{
    std::array<uint8_t,4096> reply; return accepted->channel->read(reply);
  });
  EXPECT_EQ(reading.wait_for(std::chrono::milliseconds(30)),std::future_status::timeout);
  accepted->interrupt->interrupt();
  ASSERT_EQ(reading.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  EXPECT_EQ(reading.get(),0); EXPECT_NE(fcntl(sockets[0],F_GETFD),-1);
  accepted->channel.reset(); EXPECT_EQ(fcntl(sockets[0],F_GETFD),-1);
  const int reused = open("/dev/null",O_RDONLY);
  ASSERT_EQ(reused,sockets[0]); accepted->interrupt->interrupt();
  EXPECT_NE(fcntl(reused,F_GETFD),-1);
  close(reused); close(sockets[1]);
}

TEST(NativeEventChannel, RejectedAttachmentClosesOnlyCandidateDescriptor) {
  int first[2],second[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,first),0);
  ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,second),0);
  EncryptedChannel cipher{};
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
  EncryptedChannel peer{};
  const std::array<uint8_t,32> key{};
  auto &cipher = session.pairingSession.eventChannel();
  ASSERT_TRUE(cipher.prepare(PAIR_SERVER_HOMEKIT,4,key,"","Event"));
  ASSERT_TRUE(peer.prepare(PAIR_CLIENT_HOMEKIT_NORMAL,1,key,"","Peer"));
  ASSERT_TRUE(cipher.prepared()); ASSERT_TRUE(peer.prepared());
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
  SessionState session{}; EncryptedChannel peer{};
  const std::array<uint8_t,32> key{};
  auto &cipher = session.pairingSession.eventChannel();
  ASSERT_TRUE(cipher.prepare(PAIR_SERVER_HOMEKIT,4,key,"","Event"));
  ASSERT_TRUE(peer.prepare(PAIR_CLIENT_HOMEKIT_NORMAL,1,key,"","Peer"));
  ASSERT_TRUE(cipher.prepared()); ASSERT_TRUE(peer.prepared());
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
  EXPECT_TRUE(cipher.prepared());
  cipher.release(); peer.release(); close(sockets[1]); observedReadFd = -1;
}

struct WorkerLoopback {
  int listener = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC,0);
  int client = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC,0);
  sockaddr_in address{};
  WorkerLoopback() {
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(listener,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
    socklen_t size = sizeof(address);
    EXPECT_EQ(getsockname(listener,reinterpret_cast<sockaddr *>(&address),&size),0);
    const timeval timeout{2,0};
    EXPECT_EQ(setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),0);
  }
  ~WorkerLoopback() { if (listener >= 0) close(listener); close(client); }
  void connectClient() { ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0); }
};

TEST(EventPortRuntime, NativeEncryptedTransactionsStopDuringReplyBeforeCipherRelease) {
  WorkerLoopback sockets; SessionState session{}; EncryptedChannel peer{};
  auto &cipher = session.pairingSession.eventChannel();
  const std::array<uint8_t,32> key{};
  ASSERT_TRUE(cipher.prepare(PAIR_SERVER_HOMEKIT,4,key,"","Event"));
  ASSERT_TRUE(peer.prepare(PAIR_CLIENT_HOMEKIT_NORMAL,1,key,"","Peer"));
  ASSERT_TRUE(cipher.prepared()); ASSERT_TRUE(peer.prepared());
  std::atomic<int> accepted{-1}; std::vector<ssize_t> counts;
  auto reading = cooperativeReading.get_future();
  std::promise<void> completed; auto done = completed.get_future();
  ASSERT_TRUE(session.eventPortWorker.start(std::exchange(sockets.listener,-1),session.eventCommandExchange,
      [&](int fd) { accepted = fd; return makeInterruptibleNativeEventChannel(fd,cipher); },[&] {
        auto command = plist_new_string("ordinary command");
        for (int i = 0; i < 3; ++i) {
          if (i == 2) cooperativeReadFd = accepted.load();
          counts.push_back(ap2_event_port_post_command(&session,command));
        }
        plist_free(command); completed.set_value();
      }));
  sockets.connectClient(); std::vector<uint8_t> first;
  for (int i = 0; i < 3; ++i) {
    std::array<uint8_t,4096> request{};
    const auto count = read_encrypted(sockets.client,&peer,request.data(),request.size());
    ASSERT_GT(count,0);
    const std::vector<uint8_t> bytes(request.begin(),request.begin()+count);
    if (i == 0) first = bytes; else EXPECT_EQ(bytes,first);
    if (i < 2) {
      const std::array<uint8_t,3> reply{'o',0,'k'};
      EXPECT_EQ(write_encrypted(sockets.client,&peer,reply.data(),reply.size()),3);
    }
  }
  ASSERT_EQ(reading.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  EXPECT_EQ(done.wait_for(std::chrono::milliseconds(30)),std::future_status::timeout);
  auto stopping = std::async(std::launch::async,[&]{ return session.eventPortWorker.stop(); });
  ASSERT_EQ(stopping.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  EXPECT_EQ(stopping.get(),0); EXPECT_EQ(counts,(std::vector<ssize_t>{3,3,0}));
  EXPECT_EQ(fcntl(accepted.load(),F_GETFD),-1); EXPECT_TRUE(cipher.prepared());
  cipher.release(); peer.release();
}

TEST(EventPortRuntime, NativeEncryptedBlockedWriteStopsWithoutCancellation) {
  WorkerLoopback sockets; SessionState session{};
  auto &cipher = session.pairingSession.eventChannel();
  const std::array<uint8_t,32> key{};
  ASSERT_TRUE(cipher.prepare(PAIR_SERVER_HOMEKIT,4,key,"","Event"));
  ASSERT_TRUE(cipher.prepared());
  struct IgnoreBrokenPipe {
    struct sigaction previous{};
    IgnoreBrokenPipe() { struct sigaction ignored{}; ignored.sa_handler = SIG_IGN; sigaction(SIGPIPE,&ignored,&previous); }
    ~IgnoreBrokenPipe() { sigaction(SIGPIPE,&previous,nullptr); }
  } ignored;
  std::promise<void> completed; auto done = completed.get_future();
  auto writing = nativeWriting.get_future(); EventResult result{};
  ASSERT_TRUE(session.eventPortWorker.start(std::exchange(sockets.listener,-1),session.eventCommandExchange,
      [&](int fd) {
        const int size = 4096; setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size));
        observedWriteFd = fd; return makeInterruptibleNativeEventChannel(fd,cipher);
      },[&] {
        const std::vector<uint8_t> body(8*1024*1024,'x');
        class Observer : public EventObserver { void observe(const EventDiagnostic &) override {} } observer;
        result = session.eventCommandExchange.postBinaryCommand(body,observer); completed.set_value();
      }));
  sockets.connectClient();
  ASSERT_EQ(writing.wait_for(std::chrono::seconds(2)),std::future_status::ready);
  EXPECT_EQ(done.wait_for(std::chrono::milliseconds(30)),std::future_status::timeout);
  auto stopping = std::async(std::launch::async,[&]{ return session.eventPortWorker.stop(); });
  ASSERT_EQ(stopping.wait_for(std::chrono::seconds(2)),std::future_status::ready);
  EXPECT_EQ(stopping.get(),0); EXPECT_EQ(result.status,EventStatus::writeFailed);
  EXPECT_TRUE(cipher.prepared()); cipher.release();
}

TEST(EventPortRuntime, DirectSessionDestructionJoinsNativeExchangeBeforeCipherRelease) {
  WorkerLoopback sockets;
  auto session = std::make_unique<SessionState>();
  auto *active = session.get();
  ASSERT_NE(gcry_check_version(nullptr),nullptr);
  gcry_control(GCRYCTL_INITIALIZATION_FINISHED,0);
  active->pairingSession.configure(std::make_shared<const PairingOptions>(
      PairingOptions{"00:11:22:33:44:55",std::nullopt}));
  const std::array<uint8_t,3> invalid{6,1,255};
  ASSERT_EQ(active->pairingSession.setup(invalid).status,470);
  ASSERT_EQ(active->pairingSession.verify(invalid).status,470);
  auto &cipher = active->pairingSession.eventChannel();
  EncryptedChannel peer;
  const std::array<uint8_t,32> key{};
  ASSERT_TRUE(cipher.prepare(PAIR_SERVER_HOMEKIT,4,key,"","Event"));
  ASSERT_TRUE(peer.prepare(PAIR_CLIENT_HOMEKIT_NORMAL,1,key,"","Peer"));
  auto reading = destructionReading.get_future();
  EventResult result{};
  destructionCallbackCompleted = false;
  destructionCipherReleaseCount = 0;
  destructionSetupReleaseCount = destructionVerifyReleaseCount = 0;
  ASSERT_TRUE(active->eventPortWorker.start(std::exchange(sockets.listener,-1),active->eventCommandExchange,
      [&](int fd) {
        destructionAcceptedFd = fd;
        return makeInterruptibleNativeEventChannel(fd,cipher);
      },[&] {
        class Observer final : public EventObserver {
          void observe(const EventDiagnostic &) override {}
        } observer;
        const std::array<uint8_t,3> body{'a',0,'b'};
        destructionReadFd = destructionAcceptedFd.load();
        result = active->eventCommandExchange.postBinaryCommand(body,observer);
        destructionCallbackCompleted = true;
      }));
  sockets.connectClient();
  std::array<uint8_t,4096> request{};
  ASSERT_GT(read_encrypted(sockets.client,&peer,request.data(),request.size()),0);
  ASSERT_EQ(reading.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  ASSERT_FALSE(destructionCallbackCompleted.load());
  observeDestructionCipherRelease = true;
  session.reset();
  observeDestructionCipherRelease = false;
  EXPECT_TRUE(destructionCallbackCompleted.load());
  EXPECT_EQ(result.status,EventStatus::closed);
  EXPECT_EQ(destructionCipherReleaseCount,1);
  EXPECT_EQ(destructionSetupReleaseCount,1); EXPECT_EQ(destructionVerifyReleaseCount,1);
}

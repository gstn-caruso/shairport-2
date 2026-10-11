#include "protocol/rtsp/rtsp_listener.hpp"
#include "protocol/rtsp/rtsp.h"
#include "runtime/common.h"
#include "session/session_state.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <future>
#include <atomic>
#include <poll.h>
#include <semaphore>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <array>
#include <cerrno>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <string>
#include <unistd.h>

using namespace std::chrono_literals;

static std::atomic<bool> observeWait{false};
static std::binary_semaphore waiting{0};
static std::atomic<RtspListener *> stopAfterAccept{nullptr};
static int racedDescriptor = -1;
static std::atomic<bool> drainPending{false};
static std::binary_semaphore drained{0};
static bool rejectBind = false;
static std::vector<int> failedSockets;
static std::promise<int> exitRequested;
static bool isolatedChild = false;
static std::atomic<bool> captureTiming{false};
static std::promise<std::shared_ptr<SharedRtpTimingSettings>> acceptedTiming;
static sockaddr_in boundAddress{};

std::unique_ptr<SessionState> realSessionCreate(std::shared_ptr<SharedRtpTimingSettings>)
    asm("__real__ZN12SessionState6createESt10shared_ptrI23SharedRtpTimingSettingsE");
std::unique_ptr<SessionState> wrappedSessionCreate(std::shared_ptr<SharedRtpTimingSettings>)
    asm("__wrap__ZN12SessionState6createESt10shared_ptrI23SharedRtpTimingSettingsE");
std::unique_ptr<SessionState> wrappedSessionCreate(std::shared_ptr<SharedRtpTimingSettings> timing) {
  auto session = realSessionCreate(timing);
  if (captureTiming.exchange(false)) acceptedTiming.set_value(std::move(timing));
  return session;
}

static int runIsolated(const char *mode, const char *filter = "") {
  const pid_t child = fork();
  if (child == 0) {
    execl("/proc/self/exe", "rtsp-listener-test", mode, filter, nullptr);
    _exit(99);
  }
  if (child < 0)
    return 98;
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  pid_t reaped = 0;
  while (reaped == 0 && std::chrono::steady_clock::now() < deadline) {
    reaped = waitpid(child, &status, WNOHANG);
    if (reaped == 0)
      std::this_thread::sleep_for(1ms);
  }
  if (reaped != child) {
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    return 97;
  }
  return status;
}

static int failedWorkerCreation() {
  if (geteuid() == 0) {
    const auto unprivileged = getpwnam("nobody");
    if (!unprivileged || setuid(unprivileged->pw_uid) != 0 || prctl(PR_SET_DUMPABLE, 1) != 0)
      return 10;
  }
  rlimit previous{};
  if (getrlimit(RLIMIT_NPROC, &previous) != 0)
    return 11;
  auto restricted = previous;
  restricted.rlim_cur = 0;
  if (setrlimit(RLIMIT_NPROC, &restricted) != 0)
    return 12;
  int error = rtsp_listener_start(std::make_shared<SharedRtpTimingSettings>(0));
  if (setrlimit(RLIMIT_NPROC, &previous) != 0 || error != EAGAIN)
    return 13;
  rtsp_listener_stop();
  rtsp_listener_stop();
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && std::string(argv[1]) == "--listener-failed-start")
    return failedWorkerCreation();
  if (argc >= 2 && std::string(argv[1]) == "--listener-exit") {
    config.port = 0;
    observeWait = true;
    if (rtsp_listener_start(std::make_shared<SharedRtpTimingSettings>(0)) != 0 || !waiting.try_acquire_for(500ms))
      return 19;
    return 0;
  }
  if (argc >= 2 && std::string(argv[1]) == "--listener-child") {
    isolatedChild = true;
    --argc;
    ++argv;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

extern "C" int __real_accept4(int, sockaddr *, socklen_t *, int);
extern "C" int __wrap_accept4(int fd, sockaddr *remote, socklen_t *size, int flags) {
  if (drainPending.exchange(false)) {
    int discarded = __real_accept4(fd, remote, size, flags);
    if (discarded >= 0)
      close(discarded);
    drained.release();
  }
  int accepted = __real_accept4(fd, remote, size, flags);
  if (auto *owner = stopAfterAccept.exchange(nullptr); owner && accepted >= 0) {
    racedDescriptor = accepted;
    owner->requestStop();
  }
  return accepted;
}

extern "C" void __wrap_build_bonjour_strings(rtsp_conn_info *) {}
extern "C" void __wrap_mdns_register(const char **, const char **) {}

extern "C" int __real_bind(int, const sockaddr *, socklen_t);
extern "C" int __wrap_bind(int fd, const sockaddr *address, socklen_t size) {
  if (rejectBind) {
    failedSockets.push_back(fd);
    errno = EADDRINUSE;
    return -1;
  }
  int result = __real_bind(fd, address, size);
  if (result == 0 && address->sa_family == AF_INET) {
    socklen_t length = sizeof(boundAddress);
    getsockname(fd,reinterpret_cast<sockaddr *>(&boundAddress),&length);
  }
  return result;
}

extern "C" void __wrap_exit_request(int status) { exitRequested.set_value(status); }

class ListeningSocket {
public:
  ListeningSocket() {
    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    EXPECT_GE(fd, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    EXPECT_EQ(listen(fd, 5), 0);
    socklen_t size = sizeof(address);
    EXPECT_EQ(getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size), 0);
  }

  int connectClient() const {
    int client = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    EXPECT_GE(client, 0);
    EXPECT_EQ(connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof(address)), 0);
    return client;
  }

  int fd;
  sockaddr_in address{};
};

extern "C" int __real_poll(pollfd *, nfds_t, int);
extern "C" int __wrap_poll(pollfd *descriptors, nfds_t count, int timeout) {
  if (observeWait.exchange(false))
    waiting.release();
  return __real_poll(descriptors, count, timeout);
}

TEST(RtspListener, StopWakesIdleAcceptAndJoins) {
  RtspListener listener;
  std::promise<int> result;
  auto finished = result.get_future();
  observeWait = true;
  listener.start([&](std::stop_token stop) {
    sockaddr_storage remote{};
    socklen_t size = sizeof(remote);
    result.set_value(listener.accept(stop, reinterpret_cast<sockaddr *>(&remote), &size));
  });
  waiting.acquire();
  EXPECT_EQ(finished.wait_for(20ms), std::future_status::timeout);
  listener.requestStop();
  ASSERT_EQ(finished.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(finished.get(), -1);
  listener.stop();
}

TEST(RtspListener, AcceptsBlockingCloseOnExecClientThenReleasesListeningSocket) {
  RtspListener listener;
  ListeningSocket socket;
  ASSERT_TRUE(listener.addSocket(socket.fd));
  int client = socket.connectClient();
  std::promise<int> result;
  auto finished = result.get_future();
  listener.start([&](std::stop_token stop) {
    sockaddr_storage remote{};
    socklen_t size = sizeof(remote);
    result.set_value(listener.accept(stop, reinterpret_cast<sockaddr *>(&remote), &size));
  });
  ASSERT_EQ(finished.wait_for(500ms), std::future_status::ready);
  int accepted = finished.get();
  ASSERT_GE(accepted, 0);
  EXPECT_EQ(fcntl(accepted, F_GETFL) & O_NONBLOCK, 0);
  EXPECT_NE(fcntl(accepted, F_GETFD) & FD_CLOEXEC, 0);
  close(accepted);
  close(client);
  listener.stop();
  EXPECT_EQ(fcntl(socket.fd, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST(RtspListener, StopBeforeWaitLeavesQueuedConnectionUnaccepted) {
  RtspListener listener;
  ListeningSocket socket;
  ASSERT_TRUE(listener.addSocket(socket.fd));
  int client = socket.connectClient();
  std::promise<void> entering;
  std::promise<void> resume;
  auto admitted = resume.get_future();
  int accepted = 0;
  listener.start([&](std::stop_token stop) {
    entering.set_value();
    admitted.wait();
    sockaddr_storage remote{};
    socklen_t size = sizeof(remote);
    accepted = listener.accept(stop, reinterpret_cast<sockaddr *>(&remote), &size);
  });
  entering.get_future().wait();
  listener.requestStop();
  listener.requestStop();
  resume.set_value();
  listener.stop();
  listener.stop();
  EXPECT_EQ(accepted, -1);
  close(client);
}

TEST(RtspListener, StopRacingAcceptClosesAcceptedDescriptor) {
  RtspListener listener;
  ListeningSocket socket;
  ASSERT_TRUE(listener.addSocket(socket.fd));
  int client = socket.connectClient();
  stopAfterAccept = &listener;
  std::promise<int> result;
  auto finished = result.get_future();
  listener.start([&](std::stop_token stop) {
    sockaddr_storage remote{};
    socklen_t size = sizeof(remote);
    result.set_value(listener.accept(stop, reinterpret_cast<sockaddr *>(&remote), &size));
  });
  ASSERT_EQ(finished.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(finished.get(), -1);
  listener.stop();
  ASSERT_GE(racedDescriptor, 0);
  EXPECT_EQ(fcntl(racedDescriptor, F_GETFD), -1);
  EXPECT_EQ(errno, EBADF);
  close(client);
}

TEST(RtspListener, StopWakesAfterReadinessDisappearsBeforeAccept) {
  RtspListener listener;
  ListeningSocket socket;
  ASSERT_TRUE(listener.addSocket(socket.fd));
  int client = socket.connectClient();
  drainPending = true;
  std::promise<int> result;
  auto finished = result.get_future();
  listener.start([&](std::stop_token stop) {
    sockaddr_storage remote{};
    socklen_t size = sizeof(remote);
    result.set_value(listener.accept(stop, reinterpret_cast<sockaddr *>(&remote), &size));
  });
  ASSERT_TRUE(drained.try_acquire_for(500ms));
  EXPECT_EQ(finished.wait_for(20ms), std::future_status::timeout);
  listener.requestStop();
  ASSERT_EQ(finished.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(finished.get(), -1);
  listener.stop();
  close(client);
}

TEST(RtspListener, ProtocolLoopStopsWhileWaitingForConnection) {
  if (!isolatedChild) {
    EXPECT_EQ(runIsolated("--listener-child",
                         "--gtest_filter=RtspListener.ProtocolLoopStopsWhileWaitingForConnection"), 0);
    return;
  }
  config.port = 0;
  observeWait = true;
  ASSERT_EQ(rtsp_listener_start(std::make_shared<SharedRtpTimingSettings>(0)), 0);
  ASSERT_TRUE(waiting.try_acquire_for(500ms));
  auto stop = std::async(std::launch::async, rtsp_listener_stop);
  EXPECT_EQ(stop.wait_for(500ms), std::future_status::ready);
  stop.get();
  rtsp_listener_stop();
}

TEST(RtspListener, AcceptedSessionReceivesExactOwnerAndRetainsItAfterListenerStops) {
  if (!isolatedChild) {
    EXPECT_EQ(runIsolated("--listener-child",
        "--gtest_filter=RtspListener.AcceptedSessionReceivesExactOwnerAndRetainsItAfterListenerStops"), 0);
    return;
  }
  config.port = 0;
  observeWait = true;
  captureTiming = true;
  auto timing = std::make_shared<SharedRtpTimingSettings>(-0.125);
  auto *identity = timing.get();
  auto accepted = acceptedTiming.get_future();
  ASSERT_EQ(rtsp_listener_start(timing),0);
  ASSERT_TRUE(waiting.try_acquire_for(500ms));
  const int client = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC,0);
  ASSERT_GE(client,0);
  boundAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&boundAddress),sizeof(boundAddress)),0);
  ASSERT_EQ(accepted.wait_for(500ms),std::future_status::ready);
  auto passed = accepted.get();
  EXPECT_EQ(passed.get(),identity);
  auto retained = SessionState::create(passed);
  timing.reset(); passed.reset();
  rtsp_listener_stop();
  close(client);
  EXPECT_EQ(&retained->timingSettings(),identity);
  EXPECT_DOUBLE_EQ(retained->timingSettings().offsetSeconds(),-0.125);
  retained->timingSettings().disableForInsufficientLatency();
  EXPECT_DOUBLE_EQ(retained->timingSettings().offsetSeconds(),0);
}

TEST(RtspListener, MissingTimingOwnerDoesNotStartListener) {
  EXPECT_EQ(rtsp_listener_start(nullptr),EINVAL);
}

TEST(RtspListener, FailedBindRequestsExitAndReleasesEverySocket) {
  if (!isolatedChild) {
    EXPECT_EQ(runIsolated("--listener-child",
                         "--gtest_filter=RtspListener.FailedBindRequestsExitAndReleasesEverySocket"), 0);
    return;
  }
  config.port = 0;
  rejectBind = true;
  auto failed = exitRequested.get_future();
  ASSERT_EQ(rtsp_listener_start(std::make_shared<SharedRtpTimingSettings>(0)), 0);
  ASSERT_EQ(failed.wait_for(500ms), std::future_status::ready);
  EXPECT_EQ(failed.get(), EXIT_FAILURE);
  rtsp_listener_stop();
  ASSERT_FALSE(failedSockets.empty());
  for (int descriptor : failedSockets) {
    EXPECT_EQ(fcntl(descriptor, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
  }
}

TEST(RtspListener, FailedWorkerCreationLeavesExitStopSafe) {
  EXPECT_EQ(runIsolated("--listener-failed-start"), 0);
}

TEST(RtspListener, ProcessExitStopsRunningListenerBeforeOwnerDestruction) {
  EXPECT_EQ(runIsolated("--listener-exit"), 0);
}

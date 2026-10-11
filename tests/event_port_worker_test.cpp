#include "transport/event_port_worker.hpp"
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <future>
#include <chrono>
#include <atomic>
#include <condition_variable>

struct EventIoState {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false,interrupted = false,blockWrite = false;
  std::atomic<unsigned> initial{0},closed{0};
  std::atomic<int> accepted{-1};
};
class WorkerTestChannel : public EventPlaintextChannel {
public:
  WorkerTestChannel(int fd,std::shared_ptr<EventIoState> state) : fd_(fd),state_(state) { state_->accepted = fd; }
  ~WorkerTestChannel() { close(fd_); ++state_->closed; }
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    if (state_->blockWrite) { waitForInterrupt(); return -1; }
    return bytes.size();
  }
  std::ptrdiff_t read(std::span<uint8_t>) override { waitForInterrupt(); return 0; }
private:
  void waitForInterrupt() {
    std::unique_lock lock(state_->mutex); state_->entered = true; state_->changed.notify_all();
    state_->changed.wait(lock,[&]{ return state_->interrupted; });
  }
  int fd_;
  std::shared_ptr<EventIoState> state_;
};
class WorkerTestInterrupt : public EventChannelInterrupt {
public:
  explicit WorkerTestInterrupt(std::shared_ptr<EventIoState> state) : state_(state) {}
  void interrupt() noexcept override {
    { std::lock_guard lock(state_->mutex); state_->interrupted = true; }
    state_->changed.notify_all();
  }
private:
  std::shared_ptr<EventIoState> state_;
};
static auto testFactory(std::shared_ptr<EventIoState> state) {
  return [state](int fd)->std::expected<AcceptedEventChannel,int> {
    return AcceptedEventChannel{std::make_unique<WorkerTestChannel>(fd,state),std::make_shared<WorkerTestInterrupt>(state)};
  };
}
class WorkerTestObserver : public EventObserver { void observe(const EventDiagnostic &) override {} };

struct BoundEventListener {
  int fd = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC,0);
  sockaddr_in address{};
  BoundEventListener() {
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
    socklen_t size = sizeof(address);
    EXPECT_EQ(getsockname(fd,reinterpret_cast<sockaddr *>(&address),&size),0);
  }
  ~BoundEventListener() { if (fd >= 0) close(fd); }
};

TEST(EventPortWorker, StopBeforeAcceptWakesAndClosesOwnedListener) {
  BoundEventListener listener;
  const int fd = listener.fd;
  EventCommandExchange exchange;
  EventPortWorker worker;
  unsigned calls = 0;
  ASSERT_TRUE(worker.start(std::exchange(listener.fd,-1),exchange,
      [](int)->std::expected<AcceptedEventChannel,int> { return std::unexpected(EINVAL); },[&]{ ++calls; }));
  auto stopping = std::async(std::launch::async,[&]{ return worker.stop(); });
  ASSERT_EQ(stopping.wait_for(std::chrono::milliseconds(500)),std::future_status::ready);
  EXPECT_EQ(stopping.get(),0); EXPECT_EQ(fcntl(fd,F_GETFD),-1); EXPECT_EQ(calls,0u);
  EXPECT_EQ(worker.stop(),0);
}

TEST(EventPortWorker, AcceptsOneClientAndInitializesOnceBeforeIdleStop) {
  BoundEventListener listener; const int fd = listener.fd;
  EventCommandExchange exchange; EventPortWorker worker;
  auto state = std::make_shared<EventIoState>(); std::promise<void> initialized;
  auto ready = initialized.get_future();
  ASSERT_TRUE(worker.start(std::exchange(listener.fd,-1),exchange,testFactory(state),[&]{ ++state->initial; initialized.set_value(); }));
  const int client = socket(AF_INET,SOCK_STREAM,0);
  ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&listener.address),sizeof(listener.address)),0);
  ASSERT_EQ(ready.wait_for(std::chrono::milliseconds(500)),std::future_status::ready);
  EXPECT_EQ(worker.stop(),0); EXPECT_EQ(state->initial.load(),1u); EXPECT_EQ(state->closed.load(),1u);
  EXPECT_EQ(fcntl(fd,F_GETFD),-1); EXPECT_EQ(fcntl(state->accepted.load(),F_GETFD),-1); close(client);
}

TEST(EventPortWorker, InterruptsBlockedReadBeforeJoinAndExchangeClose) {
  for (bool write : {false,true}) {
    BoundEventListener listener;
    EventCommandExchange exchange; EventPortWorker worker;
    auto state = std::make_shared<EventIoState>(); state->blockWrite = write;
    ASSERT_TRUE(worker.start(std::exchange(listener.fd,-1),exchange,testFactory(state),[&]{
      ++state->initial; WorkerTestObserver observer; exchange.postBinaryCommand({},observer);
    }));
    const int client = socket(AF_INET,SOCK_STREAM,0);
    ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&listener.address),sizeof(listener.address)),0);
    { std::unique_lock lock(state->mutex); ASSERT_TRUE(state->changed.wait_for(lock,std::chrono::seconds(1),[&]{ return state->entered; })); }
    auto stopping = std::async(std::launch::async,[&]{ return worker.stop(); });
    ASSERT_EQ(stopping.wait_for(std::chrono::milliseconds(500)),std::future_status::ready);
    EXPECT_EQ(stopping.get(),0); EXPECT_TRUE(state->interrupted); EXPECT_EQ(state->closed.load(),1u);
    close(client);
  }
}

TEST(EventPortWorker, StartupConsumesInvalidOrUnlistenableDescriptorOnFailure) {
  EventCommandExchange exchange;
  EventPortWorker worker;
  auto factory = [](int)->std::expected<AcceptedEventChannel,int> { return std::unexpected(EINVAL); };
  EXPECT_FALSE(worker.start(-1,exchange,factory,[]{}));
  const int fd = socket(AF_INET,SOCK_DGRAM,0);
  ASSERT_GE(fd,0);
  EXPECT_FALSE(worker.start(fd,exchange,factory,[]{}));
  EXPECT_EQ(fcntl(fd,F_GETFD),-1);
  EXPECT_EQ(worker.stop(),0);
}

TEST(EventPortWorker, StopDuringFactoryRegistrationInterruptsAndRejectsLateChannel) {
  BoundEventListener listener;
  EventCommandExchange exchange; EventPortWorker worker;
  auto state = std::make_shared<EventIoState>();
  std::promise<void> entered,release;
  auto reached = entered.get_future(); auto proceed = release.get_future();
  ASSERT_TRUE(worker.start(std::exchange(listener.fd,-1),exchange,
      [&](int fd)->std::expected<AcceptedEventChannel,int> {
        entered.set_value(); proceed.wait(); return testFactory(state)(fd);
      },[&]{ ++state->initial; }));
  const int client = socket(AF_INET,SOCK_STREAM,0);
  ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&listener.address),sizeof(listener.address)),0);
  ASSERT_EQ(reached.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  auto stopping = std::async(std::launch::async,[&]{ return worker.stop(); });
  EXPECT_EQ(stopping.wait_for(std::chrono::milliseconds(30)),std::future_status::timeout);
  release.set_value();
  ASSERT_EQ(stopping.wait_for(std::chrono::seconds(1)),std::future_status::ready);
  EXPECT_EQ(stopping.get(),0); EXPECT_TRUE(state->interrupted);
  EXPECT_EQ(state->initial.load(),0u); EXPECT_EQ(state->closed.load(),1u);
  close(client);
}

TEST(EventPortWorker, ConcurrentStopsJoinOnceAndDuplicateStartConsumesOnlyCandidate) {
  BoundEventListener listener,candidate;
  EventCommandExchange exchange; EventPortWorker worker;
  auto state = std::make_shared<EventIoState>();
  ASSERT_TRUE(worker.start(std::exchange(listener.fd,-1),exchange,testFactory(state),[]{}));
  const int fd = candidate.fd;
  auto duplicate = worker.start(std::exchange(candidate.fd,-1),exchange,testFactory(state),[]{});
  ASSERT_FALSE(duplicate); EXPECT_EQ(duplicate.error(),EALREADY); EXPECT_EQ(fcntl(fd,F_GETFD),-1);
  auto first = std::async(std::launch::async,[&]{ return worker.stop(); });
  auto second = std::async(std::launch::async,[&]{ return worker.stop(); });
  EXPECT_EQ(first.get(),0); EXPECT_EQ(second.get(),0); EXPECT_EQ(worker.stop(),0);
}

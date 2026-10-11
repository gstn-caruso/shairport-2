#include "protocol/ap2/event_command_exchange.hpp"
#include <gtest/gtest.h>
#include <vector>
#include <array>
#include <algorithm>
#include <future>
#include <condition_variable>
#include <thread>
#include "cancellation_wait.hpp"

struct EventTransactionState {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false,release = false,destroyed = false;
  std::vector<std::string> sequence;
};
class BlockingEventChannel : public EventPlaintextChannel {
public:
  explicit BlockingEventChannel(std::shared_ptr<EventTransactionState> state) : state_(state) {}
  ~BlockingEventChannel() { std::lock_guard lock(state_->mutex); state_->destroyed = true; }
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    std::lock_guard lock(state_->mutex); current_ = bytes.back();
    state_->sequence.push_back(std::string("write")+current_); return bytes.size();
  }
  std::ptrdiff_t read(std::span<uint8_t>) override {
    std::unique_lock lock(state_->mutex);
    const char command = current_;
    state_->entered = true; state_->changed.notify_all();
    state_->changed.wait(lock,[&]{ return state_->release; });
    state_->sequence.push_back(std::string("read")+command); return 0;
  }
private:
  std::shared_ptr<EventTransactionState> state_;
  char current_ = 0;
};

class EventRecordingObserver : public EventObserver {
public:
  std::vector<EventDiagnostic> events;
  void observe(const EventDiagnostic &event) override { events.push_back({event.kind,event.count,{}}); }
};
class EventRecordingChannel : public EventPlaintextChannel {
public:
  std::vector<uint8_t> written;
  unsigned reads = 0;
  std::ptrdiff_t writeResult = -2,readResult = 0;
  std::vector<uint8_t> reply;
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    written.assign(bytes.begin(),bytes.end()); return writeResult == -2 ? bytes.size() : writeResult;
  }
  std::ptrdiff_t read(std::span<uint8_t> bytes) override {
    ++reads; std::copy(reply.begin(),reply.end(),bytes.begin()); return readResult;
  }
};

TEST(EventCommandExchange, MissingChannelReportsNotOpenWithoutIo) {
  EventCommandExchange exchange;
  EventRecordingObserver observer;
  auto result = exchange.postBinaryCommand({},observer);
  EXPECT_EQ(result.status,EventStatus::notOpen); EXPECT_EQ(result.count,-1);
  ASSERT_EQ(observer.events.size(),1u);
  EXPECT_EQ(observer.events[0].kind,EventDiagnosticKind::notOpen);
}

TEST(EventCommandExchange, ShortOrFailedWriteNeverReadsAndReturnsLegacyError) {
  for (auto count : {-1,0,3}) {
    EventCommandExchange exchange;
    auto channel = std::make_unique<EventRecordingChannel>(); auto *recording = channel.get();
    recording->writeResult = count;
    ASSERT_TRUE(exchange.attach(std::move(channel)));
    EventRecordingObserver observer;
    auto result = exchange.postBinaryCommand({},observer);
    EXPECT_EQ(result.status,EventStatus::writeFailed); EXPECT_EQ(result.count,-1);
    EXPECT_EQ(recording->reads,0u); EXPECT_TRUE(observer.events.empty());
  }
}

TEST(EventCommandExchange, ReplyErrorRetainsSignedCountAndDiagnosticOrder) {
  EventCommandExchange exchange;
  auto channel = std::make_unique<EventRecordingChannel>(); channel->readResult = -1;
  ASSERT_TRUE(exchange.attach(std::move(channel)));
  EventRecordingObserver observer;
  auto result = exchange.postBinaryCommand({},observer);
  EXPECT_EQ(result.status,EventStatus::readFailed); EXPECT_EQ(result.count,-1);
  ASSERT_EQ(observer.events.size(),3u); EXPECT_EQ(observer.events[1].count,-1);
  EXPECT_EQ(observer.events[2].kind,EventDiagnosticKind::closed);
}

TEST(EventCommandExchange, OwnsBinaryAndFullCapacityReplyWithoutTerminator) {
  for (auto reply : {std::vector<uint8_t>{1,0,0xff,2},std::vector<uint8_t>(4096,0xa5)}) {
    EventCommandExchange exchange;
    auto channel = std::make_unique<EventRecordingChannel>();
    channel->readResult = reply.size(); channel->reply = reply;
    ASSERT_TRUE(exchange.attach(std::move(channel)));
    EventRecordingObserver observer;
    auto result = exchange.postBinaryCommand({},observer);
    exchange.close();
    EXPECT_EQ(result.count,static_cast<std::ptrdiff_t>(reply.size())); EXPECT_EQ(result.reply,reply);
    ASSERT_EQ(observer.events.size(),3u); EXPECT_EQ(observer.events[2].kind,EventDiagnosticKind::reply);
  }
}

TEST(EventCommandExchange, SerializesCompleteWriteReadPairsBeforeSecondCommand) {
  EventCommandExchange exchange;
  auto state = std::make_shared<EventTransactionState>();
  ASSERT_TRUE(exchange.attach(std::make_unique<BlockingEventChannel>(state)));
  EventRecordingObserver firstObserver,secondObserver;
  const std::array<uint8_t,1> firstBody{'1'},secondBody{'2'};
  auto first = std::async(std::launch::async,[&]{ return exchange.postBinaryCommand(firstBody,firstObserver); });
  { std::unique_lock lock(state->mutex); ASSERT_TRUE(state->changed.wait_for(lock,std::chrono::seconds(1),[&]{ return state->entered; })); }
  auto second = std::async(std::launch::async,[&]{ return exchange.postBinaryCommand(secondBody,secondObserver); });
  { std::lock_guard lock(state->mutex); state->release = true; } state->changed.notify_all();
  EXPECT_EQ(first.get().count,0); EXPECT_EQ(second.get().count,0);
  EXPECT_EQ(state->sequence,(std::vector<std::string>{"write1","read1","write2","read2"}));
}

TEST(EventCommandExchange, CloseWaitsForExchangeThenDestroysChannelOnce) {
  EventCommandExchange exchange;
  auto state = std::make_shared<EventTransactionState>();
  ASSERT_TRUE(exchange.attach(std::make_unique<BlockingEventChannel>(state)));
  EventRecordingObserver observer;
  const std::array<uint8_t,1> body{'1'};
  auto posting = std::async(std::launch::async,[&]{ return exchange.postBinaryCommand(body,observer); });
  { std::unique_lock lock(state->mutex); ASSERT_TRUE(state->changed.wait_for(lock,std::chrono::seconds(1),[&]{ return state->entered; })); }
  auto closing = std::async(std::launch::async,[&]{ exchange.close(); });
  EXPECT_EQ(closing.wait_for(std::chrono::milliseconds(20)),std::future_status::timeout);
  { std::lock_guard lock(state->mutex); EXPECT_FALSE(state->destroyed); state->release = true; } state->changed.notify_all();
  posting.get(); closing.get(); EXPECT_TRUE(state->destroyed); exchange.close();
  EXPECT_EQ(exchange.postBinaryCommand({},observer).status,EventStatus::notOpen);
}

class CancelledEventChannel : public EventPlaintextChannel {
public:
  CancellationWait waiting;
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override { return bytes.size(); }
  std::ptrdiff_t read(std::span<uint8_t>) override { waiting.block(); return 0; }
};
TEST(EventCommandExchange, CancelledReadUnwindsTransactionAndAllowsClose) {
  EventCommandExchange exchange;
  auto channel = std::make_unique<CancelledEventChannel>(); auto *waiting = channel.get();
  ASSERT_TRUE(exchange.attach(std::move(channel)));
  struct Context { EventCommandExchange &exchange; EventRecordingObserver observer; } context{exchange,{}};
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,[](void *arg)->void * {
    auto &context = *static_cast<Context *>(arg);
    context.exchange.postBinaryCommand({},context.observer); return nullptr;
  },&context),0);
  waiting->waiting.waitForBlocked(1);
  EXPECT_EQ(pthread_cancel(worker),0); EXPECT_EQ(pthread_join(worker,nullptr),0);
  exchange.close();
  EXPECT_EQ(exchange.postBinaryCommand({},context.observer).status,EventStatus::notOpen);
}

TEST(EventCommandExchange, RejectsMissingAndAdditionalChannelWithoutReplacingFirst) {
  EventCommandExchange exchange;
  EXPECT_FALSE(exchange.attach(nullptr));
  auto first = std::make_shared<EventTransactionState>(),rejected = std::make_shared<EventTransactionState>();
  EXPECT_TRUE(exchange.attach(std::make_unique<BlockingEventChannel>(first)));
  EXPECT_FALSE(exchange.attach(std::make_unique<BlockingEventChannel>(rejected)));
  EXPECT_TRUE(rejected->destroyed); EXPECT_FALSE(first->destroyed);
  exchange.close(); EXPECT_TRUE(first->destroyed);
}

TEST(EventCommandExchange, FramesEmptyAndBinaryCommandsWithExactHeaderOrder) {
  for (auto body : {std::vector<uint8_t>{},std::vector<uint8_t>{0,1,0xff,0}}) {
    EventCommandExchange exchange;
    auto channel = std::make_unique<EventRecordingChannel>(); auto *recording = channel.get();
    ASSERT_TRUE(exchange.attach(std::move(channel)));
    EventRecordingObserver observer;
    auto result = exchange.postBinaryCommand(body,observer);
    const std::string header = "POST /command RTSP/1.0\r\nContent-Length: "+std::to_string(body.size())+
        "\r\nContent-Type: application/x-apple-binary-plist\r\n\r\n";
    std::vector<uint8_t> expected(header.begin(),header.end()); expected.insert(expected.end(),body.begin(),body.end());
    EXPECT_EQ(recording->written,expected); EXPECT_EQ(recording->reads,1u);
    EXPECT_EQ(result.count,0); EXPECT_EQ(result.status,EventStatus::closed);
    ASSERT_EQ(observer.events.size(),3u);
    EXPECT_EQ(observer.events[0].kind,EventDiagnosticKind::written);
    EXPECT_EQ(observer.events[1].kind,EventDiagnosticKind::read);
    EXPECT_EQ(observer.events[2].kind,EventDiagnosticKind::closed);
  }
}

#include "protocol/ap2/event_command_exchange.hpp"
#include <gtest/gtest.h>
#include <vector>
#include <array>
#include <algorithm>

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

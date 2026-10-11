#include "protocol/ap2/event_command_exchange.hpp"
#include <gtest/gtest.h>
#include <vector>

class EventRecordingObserver : public EventObserver {
public:
  std::vector<EventDiagnostic> events;
  void observe(const EventDiagnostic &event) override { events.push_back({event.kind,event.count,{}}); }
};
class EventRecordingChannel : public EventPlaintextChannel {
public:
  std::vector<uint8_t> written;
  unsigned reads = 0;
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    written.assign(bytes.begin(),bytes.end()); return bytes.size();
  }
  std::ptrdiff_t read(std::span<uint8_t>) override { ++reads; return 0; }
};

TEST(EventCommandExchange, MissingChannelReportsNotOpenWithoutIo) {
  EventCommandExchange exchange;
  EventRecordingObserver observer;
  auto result = exchange.postBinaryCommand({},observer);
  EXPECT_EQ(result.status,EventStatus::notOpen); EXPECT_EQ(result.count,-1);
  ASSERT_EQ(observer.events.size(),1u);
  EXPECT_EQ(observer.events[0].kind,EventDiagnosticKind::notOpen);
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

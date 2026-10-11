#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <vector>
#include <span>
#include <cerrno>
import receiver.protocol.ap2.control_packet;

TEST(ControlLatency, NormalLatencyTruncatesBufferFramesAndWarningZeroUsesSourceLatency) {
  auto normal = ControlLatency::calculate(100000,177175,44100,0.10001,0.01,false);
  ASSERT_TRUE(normal);
  EXPECT_EQ(normal->stream,77175); EXPECT_EQ(normal->source,83790); EXPECT_EQ(normal->total,84231);
  EXPECT_FALSE(normal->warnAndZero);
  auto negative = ControlLatency::calculate(100000,177175,44100,0.1,-10,false);
  ASSERT_TRUE(negative); EXPECT_TRUE(negative->warnAndZero); EXPECT_EQ(negative->total,83790);
  auto warned = ControlLatency::calculate(100000,177175,44100,0.1,-10,true);
  ASSERT_TRUE(warned); EXPECT_FALSE(warned->warnAndZero); EXPECT_EQ(warned->total,-357210);
}
TEST(ControlLatency, OverflowingTimestampLatencyReturnsTypedFailure) {
  auto overflow = ControlLatency::calculate(0,0x7fffffff,44100,0,0,false);
  EXPECT_FALSE(overflow);
}
TEST(ControlLatency, NonfiniteOutOfRangeConversionsAndZeroRateRejectExplicitly) {
  for (double seconds : {std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(),1E100,-1E100}) {
    EXPECT_FALSE(ControlLatency::calculate(100000,177175,44100,seconds,0,false));
    EXPECT_FALSE(ControlLatency::calculate(100000,177175,44100,0,seconds,false));
  }
  EXPECT_FALSE(ControlLatency::calculate(100000,177175,0,0,0,false));
  EXPECT_FALSE(ControlLatency::calculate(100000,177175,1,0,2147483647.9,false));
}
TEST(ControlLatency, WrappedFrameDifferenceAndFractionalIntBoundaryPreserveDefinedBehavior) {
  auto wrapped = ControlLatency::calculate(0xfffffff0,77159,44100,0,0,false);
  ASSERT_TRUE(wrapped); EXPECT_EQ(wrapped->stream,77175); EXPECT_EQ(wrapped->total,88200);
  auto boundary = ControlLatency::calculate(0,0xffffd4ef,1,0,2147483647.9,true);
  ASSERT_TRUE(boundary); EXPECT_EQ(boundary->source,0); EXPECT_EQ(boundary->total,INT32_MAX);
}
static std::vector<uint8_t> controlBytes(uint8_t type = 0xd6,bool sentinel = true) {
  std::vector<uint8_t> bytes(28); bytes[0] = sentinel ? 0x10 : 0; bytes[1] = type; return bytes;
}
static ControlLiveSettings settings(bool realtime = true) { return {realtime,44100,0.1,0,false,0}; }
TEST(ControlPacketCoordinator, FirstErrorInitializesAndShortPacketDoesNotDraw) {
  ControlPacketCoordinator coordinator(1000000000);
  unsigned draws = 0;
  const auto random = [&]{ ++draws; return 1.0; };
  auto error = coordinator.handle({{},EIO,1000000000},settings(),random);
  ASSERT_EQ(error.size(),2u);
  EXPECT_EQ(error[0].kind,ControlEffectKind::initialized);
  EXPECT_EQ(error[1].kind,ControlEffectKind::error); EXPECT_EQ(error[1].errorCode,EIO);
  std::vector<uint8_t> shortPacket(27);
  auto malformed = coordinator.handle({shortPacket,0,1000000000},settings(),random);
  ASSERT_EQ(malformed.size(),1u); EXPECT_EQ(malformed[0].kind,ControlEffectKind::malformed);
  EXPECT_EQ(malformed[0].bytes.size(),27u); EXPECT_EQ(draws,0u);
}
TEST(ControlPacketCoordinator, StrictTwoMillisecondStaleGateAndSentinelBypassControlCounting) {
  ControlPacketCoordinator coordinator(1000000000);
  unsigned draws = 0;
  const auto random = [&]{ ++draws; return 1.0; };
  auto packet = controlBytes(0xd6,false);
  auto stale = coordinator.handle({packet,0,1001999999},settings(),random);
  ASSERT_EQ(stale.size(),2u); EXPECT_EQ(stale[1].kind,ControlEffectKind::stale);
  EXPECT_EQ(stale[1].elapsedNs,1999999); EXPECT_EQ(draws,0u);
  auto boundary = coordinator.handle({packet,0,1002000000},settings(),random);
  ASSERT_EQ(boundary.size(),3u);
  EXPECT_EQ(boundary[0].kind,ControlEffectKind::firstSentinel); EXPECT_FALSE(boundary[0].sentinel);
  EXPECT_EQ(boundary[1].kind,ControlEffectKind::capturePeerIfAbsent);
  EXPECT_EQ(boundary[2].kind,ControlEffectKind::retransmit);
  EXPECT_EQ(boundary[2].bytes.data(),packet.data()+6);
  EXPECT_EQ(boundary[2].bytes.size(),22u);
  ControlPacketCoordinator sentinel(1000000000);
  packet[0] = 0x10;
  auto first = sentinel.handle({packet,0,999999999},settings(),random);
  ASSERT_EQ(first.size(),4u); EXPECT_TRUE(first[1].sentinel);
}
TEST(ControlPacketCoordinator, FirstPacketCountAdvancesBeforeRandomDropAndZeroBypassesDraw) {
  ControlPacketCoordinator coordinator(0);
  auto packet = controlBytes();
  auto live = settings(); live.dropFraction = 0.5;
  unsigned draws = 0;
  auto dropped = coordinator.handle({packet,0,0},live,[&]{ ++draws; return 0.5; });
  ASSERT_EQ(dropped.size(),3u);
  EXPECT_EQ(dropped[1].kind,ControlEffectKind::firstSentinel);
  EXPECT_EQ(dropped[2].kind,ControlEffectKind::diagnosticDrop);
  auto admitted = coordinator.handle({packet,0,0},live,[&]{ ++draws; return 0.500001; });
  ASSERT_EQ(admitted.size(),2u); EXPECT_EQ(admitted[0].kind,ControlEffectKind::capturePeerIfAbsent);
  EXPECT_EQ(draws,2u);
  live.dropFraction = 0;
  coordinator.handle({packet,0,0},live,[&]{ ++draws; return 0; });
  EXPECT_EQ(draws,2u);
}
TEST(ControlPacketCoordinator, OnlyRealtimeTimeoutResetsCountAndRearmsFirstSentinel) {
  ControlPacketCoordinator coordinator(0);
  auto packet = controlBytes(); const auto random = []{ return 1.0; };
  coordinator.handle({packet,0,0},settings(),random);
  EXPECT_EQ(coordinator.handle({{},EAGAIN,1},settings(false),random).size(),0u);
  auto still = coordinator.handle({packet,0,2},settings(),random);
  ASSERT_EQ(still.size(),2u); EXPECT_EQ(still[0].kind,ControlEffectKind::capturePeerIfAbsent);
  auto timeout = coordinator.handle({{},EAGAIN,3},settings(),random);
  ASSERT_EQ(timeout.size(),1u); EXPECT_EQ(timeout[0].kind,ControlEffectKind::resetAnchor);
  auto restarted = coordinator.handle({packet,0,4},settings(),random);
  ASSERT_EQ(restarted.size(),3u); EXPECT_EQ(restarted[0].kind,ControlEffectKind::firstSentinel);
}
static std::vector<uint8_t> controlAnchor(uint32_t first = 100000,uint32_t second = 177175) {
  auto bytes = controlBytes(0xd7);
  const auto write = [&bytes](size_t offset,uint64_t value,unsigned count) {
    for (unsigned index = 0; index < count; ++index) bytes[offset+index] = value >> (8*(count-index-1));
  };
  write(4,first,4); write(8,0x0102030405060708,8); write(16,second,4); write(20,0x1122334455667788,8);
  return bytes;
}
TEST(ControlPacketCoordinator, AdmittedAnchorCopiesBigEndianFieldsAndOrdersPeerLatencyThenAnchor) {
  ControlPacketCoordinator coordinator(0); auto packet = controlAnchor();
  auto decision = coordinator.handle({packet,0,0},settings(),[]{ return 1.0; });
  ASSERT_EQ(decision.size(),5u);
  EXPECT_EQ(decision[0].kind,ControlEffectKind::initialized);
  EXPECT_EQ(decision[1].kind,ControlEffectKind::firstSentinel);
  EXPECT_EQ(decision[2].kind,ControlEffectKind::capturePeerIfAbsent);
  EXPECT_EQ(decision[3].kind,ControlEffectKind::setLatency); EXPECT_EQ(decision[3].latency,83790);
  EXPECT_EQ(decision[4].kind,ControlEffectKind::setAnchor);
  std::fill(packet.begin(),packet.end(),0);
  EXPECT_EQ(decision[4].clockId,0x1122334455667788u);
  EXPECT_EQ(decision[4].timeNs,0x0102030405060708u);
  EXPECT_EQ(decision[4].anchorFrame,88975u);
}
TEST(ControlPacketCoordinator, SharedOffsetWarningIsExplicitAndNextPacketObservesLiveSettings) {
  ControlPacketCoordinator coordinator(0); auto packet = controlAnchor(); auto live = settings();
  live.sharedOffsetSeconds = -10;
  auto first = coordinator.handle({packet,0,0},live,[]{ return 1.0; });
  ASSERT_EQ(first.size(),6u);
  EXPECT_EQ(first[3].kind,ControlEffectKind::warnAndZeroSharedOffset);
  EXPECT_EQ(first[4].latency,83790);
  live.warningIssued = true; live.sharedOffsetSeconds = 0;
  auto changed = coordinator.handle({packet,0,1},live,[]{ return 1.0; });
  ASSERT_EQ(changed.size(),3u); EXPECT_EQ(changed[1].latency,83790);
  live.sharedOffsetSeconds = -10;
  auto alreadyWarned = coordinator.handle({packet,0,2},live,[]{ return 1.0; });
  ASSERT_EQ(alreadyWarned.size(),3u); EXPECT_EQ(alreadyWarned[1].latency,-357210);
}
TEST(ControlPacketCoordinator, UnusualLatencyAndWrappingAnchorStayTypedWhileInvalidLatencyCannotPublish) {
  ControlPacketCoordinator coordinator(0); auto unusual = controlAnchor(0,77176);
  auto normal = coordinator.handle({unusual,0,0},settings(),[]{ return 1.0; });
  ASSERT_EQ(normal.size(),6u); EXPECT_EQ(normal[3].kind,ControlEffectKind::unexpectedLatency);
  EXPECT_EQ(normal[3].stream,77176); EXPECT_EQ(normal[5].anchorFrame,uint32_t{0}-11025u);
  auto invalid = controlAnchor(0,0x7fffffff);
  auto rejected = coordinator.handle({invalid,0,1},settings(),[]{ return 1.0; });
  ASSERT_EQ(rejected.size(),3u); EXPECT_EQ(rejected[0].kind,ControlEffectKind::capturePeerIfAbsent);
  EXPECT_EQ(rejected[1].kind,ControlEffectKind::unexpectedLatency);
  EXPECT_EQ(rejected[2].kind,ControlEffectKind::invalidLatency);
}
TEST(ControlPacketCoordinator, UnknownAndRetransmitViewsBorrowOnlyCurrentPacketStorage) {
  ControlPacketCoordinator coordinator(0); auto bytes = controlBytes(0x42);
  auto unknown = coordinator.handle({bytes,0,0},settings(),[]{ return 1.0; });
  ASSERT_EQ(unknown.size(),4u); EXPECT_EQ(unknown[3].kind,ControlEffectKind::unknown);
  EXPECT_EQ(unknown[3].bytes.data(),bytes.data()); EXPECT_EQ(unknown[3].bytes.size(),28u);
}

#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
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

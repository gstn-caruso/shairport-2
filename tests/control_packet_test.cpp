#include <gtest/gtest.h>
#include <cstdint>
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

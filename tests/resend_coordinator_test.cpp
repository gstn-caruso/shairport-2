#include "protocol/rtp/resend_coordinator.hpp"
#include <gtest/gtest.h>
#include <vector>

class RecordingResendIo : public ResendIo {
public:
  uint64_t now = 1;
  double random = 0.5;
  ResendSendResult result{8,0,0};
  unsigned clocks = 0,draws = 0,sends = 0;
  std::vector<uint8_t> bytes;
  uint64_t nowNs() override { ++clocks; return now; }
  double draw() override { ++draws; return random; }
  ResendSendResult send(std::span<const uint8_t,8> packet) override {
    ++sends; bytes.assign(packet.begin(),packet.end()); return result;
  }
};

TEST(ResendCoordinator, InactiveAndMissingPeerAvoidAllIo) {
  ResendCoordinator coordinator;
  RecordingResendIo io;
  EXPECT_EQ(coordinator.request(false,true,1,1,0,io).status,ResendStatus::inactive);
  EXPECT_EQ(coordinator.request(true,false,1,1,0,io).status,ResendStatus::noPeer);
  EXPECT_EQ(io.clocks+io.draws+io.sends,0u);
}

TEST(ResendCoordinator, BuildsExactWireAndBypassesRandomAtZeroFraction) {
  ResendCoordinator coordinator;
  RecordingResendIo io;
  EXPECT_EQ(coordinator.request(true,true,0xcdef,0x12345,0,io).status,ResendStatus::sent);
  EXPECT_EQ(io.bytes,(std::vector<uint8_t>{0x80,0xd5,0,1,0xcd,0xef,0x23,0x45}));
  EXPECT_EQ(io.clocks,1u); EXPECT_EQ(io.draws,0u); EXPECT_EQ(io.sends,1u);
}

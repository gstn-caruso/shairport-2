#include "protocol/rtp/resend_coordinator.hpp"
#include <gtest/gtest.h>
#include <vector>
#include <thread>
#include <barrier>
#include "cancellation_wait.hpp"

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

TEST(ResendCoordinator, FailureSuppressesThroughExactBackoffBoundaryAndPreservesErrors) {
  ResendCoordinator coordinator;
  RecordingResendIo io;
  io.now = 100;
  io.result = {-1,17,22};
  auto failed = coordinator.request(true,true,1,1,0,io);
  EXPECT_EQ(failed.status,ResendStatus::sendFailed);
  EXPECT_EQ(failed.errorCode,17); EXPECT_EQ(failed.timeoutSetupError,22);
  io.now += 300000000;
  EXPECT_EQ(coordinator.request(true,true,1,1,0.1,io).status,ResendStatus::suppressed);
  EXPECT_EQ(io.draws,0u); EXPECT_EQ(io.sends,1u);
  ++io.now;
  io.result = {0,0,22};
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::sent);
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::sent);
  EXPECT_EQ(io.sends,3u);
}

TEST(ResendCoordinator, StrictRandomEqualityDropsAndResetClearsBackoff) {
  ResendCoordinator coordinator;
  RecordingResendIo io;
  EXPECT_EQ(coordinator.request(true,true,1,1,0.5,io).status,ResendStatus::dropped);
  EXPECT_EQ(io.draws,1u); EXPECT_EQ(io.sends,0u);
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::suppressed);
  coordinator.reset();
  io.random = 0.5001;
  EXPECT_EQ(coordinator.request(true,true,1,1,0.5,io).status,ResendStatus::sent);
}

TEST(ResendCoordinator, ZeroTimestampSentinelAndUnsignedWrapPreserveEligibility) {
  ResendCoordinator coordinator;
  RecordingResendIo io;
  io.now = 0; io.result = {-1,11,0};
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::sendFailed);
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::sendFailed);
  io.now = UINT64_MAX-100;
  coordinator.reset();
  coordinator.request(true,true,1,1,0,io);
  io.now = 300000000-101;
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::suppressed);
  ++io.now;
  io.result = {3,0,0};
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::sent);
  EXPECT_EQ(coordinator.request(true,true,1,1,0,io).status,ResendStatus::sent);
}

TEST(ResendCoordinator, ConcurrentFailureTransactionsSendOnlyOnce) {
  ResendCoordinator coordinator;
  RecordingResendIo io;
  io.result = {-1,11,0};
  std::barrier start(8);
  std::vector<std::thread> workers;
  for (unsigned index = 0; index < 8; ++index) workers.emplace_back([&] {
    start.arrive_and_wait();
    coordinator.request(true,true,1,1,0,io);
  });
  for (auto &worker : workers) worker.join();
  EXPECT_EQ(io.clocks,8u); EXPECT_EQ(io.sends,1u);
}

class CancelledResendIo : public ResendIo {
public:
  CancellationWait waiting;
  ResendCoordinator *coordinator;
  uint64_t nowNs() override { return 1; }
  double draw() override { return 1; }
  ResendSendResult send(std::span<const uint8_t,8>) override { waiting.block(); return {8,0,0}; }
};

TEST(ResendCoordinator, CancelledIoUnwindsTransactionLock) {
  ResendCoordinator coordinator;
  CancelledResendIo io;
  io.coordinator = &coordinator;
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,[](void *context)->void * {
    auto &port = *static_cast<CancelledResendIo *>(context);
    port.coordinator->request(true,true,1,1,0,port);
    return nullptr;
  },&io),0);
  io.waiting.waitForBlocked(1);
  ASSERT_EQ(pthread_cancel(worker),0);
  ASSERT_EQ(pthread_join(worker,nullptr),0);
  RecordingResendIo next;
  EXPECT_EQ(coordinator.request(true,true,1,1,0,next).status,ResendStatus::sent);
}

#include "session/session_state.hpp"
#include <gtest/gtest.h>
#include "pairing_test_directory.hpp"

TEST(SessionTimingSettings, FactoryPreservesValueInitializationAndExactSharedOwner) {
  auto timing = std::make_shared<SharedRtpTimingSettings>(-0.25);
  auto session = SessionState::create(timing,makeTestPairingDirectory(),makeTestPairingOptions());
  EXPECT_EQ(&session->timingSettings(), timing.get());
  EXPECT_EQ(session->connection_number, 0);
  EXPECT_EQ(session->UserAgent, nullptr);
  EXPECT_EQ(session->latency, 0u);
  EXPECT_EQ(session->latency_warning_issued, 0);
  EXPECT_EQ(session->rtp_data_thread, nullptr);
  EXPECT_EQ(session->sessionPlist, nullptr);
  EXPECT_EQ(session->fd, -1);
  EXPECT_TRUE(session->mayAcquirePrincipal());
}

TEST(SessionTimingSettings, ExistingAndLaterSessionsShareDisableWhileDefaultsAreIsolated) {
  auto timing = std::make_shared<SharedRtpTimingSettings>(-0.25);
  auto first = SessionState::create(timing,makeTestPairingDirectory(),makeTestPairingOptions());
  auto second = SessionState::create(timing,makeTestPairingDirectory(),makeTestPairingOptions());
  SessionState isolated{};
  EXPECT_DOUBLE_EQ(second->timingSettings().offsetSeconds(), -0.25);
  first->timingSettings().disableForInsufficientLatency();
  auto later = SessionState::create(timing,makeTestPairingDirectory(),makeTestPairingOptions());
  EXPECT_DOUBLE_EQ(second->timingSettings().offsetSeconds(), 0);
  EXPECT_DOUBLE_EQ(later->timingSettings().offsetSeconds(), 0);
  EXPECT_DOUBLE_EQ(isolated.timingSettings().offsetSeconds(), 0);
  EXPECT_NE(&isolated.timingSettings(), timing.get());
  timing.reset();
  first.reset();
  EXPECT_DOUBLE_EQ(second->timingSettings().offsetSeconds(), 0);
}

TEST(SessionTimingSettings, NullSharedOwnerIsRejectedAtFactoryBoundary) {
  EXPECT_THROW(SessionState::create(nullptr,makeTestPairingDirectory(),makeTestPairingOptions()), std::invalid_argument);
  EXPECT_THROW(SessionState::create(std::make_shared<SharedRtpTimingSettings>(0),nullptr,makeTestPairingOptions()), std::invalid_argument);
  EXPECT_THROW(SessionState::create(std::make_shared<SharedRtpTimingSettings>(0),makeTestPairingDirectory(),nullptr), std::invalid_argument);
}

TEST(SessionPairingDirectory, SharedApplicationOwnerOutlivesOtherSessionsAndIsolatesApplications) {
  auto directory = makeTestPairingDirectory(); auto *identity = directory.get();
  auto timing = std::make_shared<SharedRtpTimingSettings>(0);
  auto first = SessionState::create(timing,directory,makeTestPairingOptions()); auto second = SessionState::create(timing,directory,makeTestPairingOptions());
  auto isolated = SessionState::create(timing,makeTestPairingDirectory(),makeTestPairingOptions());
  std::array<uint8_t,32> key{}; key.fill(4);
  first->pairingDirectory().upsert("ordinary-device",key);
  EXPECT_EQ(&second->pairingDirectory(),identity);
  EXPECT_EQ(second->pairingDirectory().snapshot(),(std::vector<PairingRecord>{{"ordinary-device",key}}));
  EXPECT_TRUE(isolated->pairingDirectory().snapshot().empty());
  directory.reset(); first.reset();
  EXPECT_EQ(&second->pairingDirectory(),identity);
  EXPECT_EQ(second->pairingDirectory().snapshot().size(),1u);
}

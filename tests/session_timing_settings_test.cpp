#include "session/session_state.hpp"
#include <gtest/gtest.h>

TEST(SessionTimingSettings, FactoryPreservesValueInitializationAndExactSharedOwner) {
  auto timing = std::make_shared<SharedRtpTimingSettings>(-0.25);
  auto session = SessionState::create(timing);
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
  auto first = SessionState::create(timing);
  auto second = SessionState::create(timing);
  SessionState isolated{};
  EXPECT_DOUBLE_EQ(second->timingSettings().offsetSeconds(), -0.25);
  first->timingSettings().disableForInsufficientLatency();
  auto later = SessionState::create(timing);
  EXPECT_DOUBLE_EQ(second->timingSettings().offsetSeconds(), 0);
  EXPECT_DOUBLE_EQ(later->timingSettings().offsetSeconds(), 0);
  EXPECT_DOUBLE_EQ(isolated.timingSettings().offsetSeconds(), 0);
  EXPECT_NE(&isolated.timingSettings(), timing.get());
  timing.reset();
  first.reset();
  EXPECT_DOUBLE_EQ(second->timingSettings().offsetSeconds(), 0);
}

TEST(SessionTimingSettings, NullSharedOwnerIsRejectedAtFactoryBoundary) {
  EXPECT_THROW(SessionState::create(nullptr), std::invalid_argument);
}

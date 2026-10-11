#include "session/pairing_session.hpp"
#include <gtest/gtest.h>

TEST(PairingSession, UnconfiguredOwnerIsExplicitlyInertAndReleaseIsRepeatable) {
  PairingSession session;
  EXPECT_EQ(session.setup({}).status,500);
  EXPECT_EQ(session.verify({}).status,500);
  EXPECT_EQ(session.prepareDataChannel(42),DataPreparation::noSetupSecret);
  EXPECT_FALSE(session.controlChannel().prepared());
  session.release(); session.release();
  EXPECT_FALSE(session.eventChannel().prepared());
}

#include "transport/encrypted_channel.hpp"
#include <gtest/gtest.h>
#include <array>

static EncryptedChannel *watchedChannel = nullptr;
static int releaseCount = 0;
extern "C" void __real_pair_cipher_free(pair_cipher_context *cipher);
extern "C" void __wrap_pair_cipher_free(pair_cipher_context *cipher) {
  if (cipher && watchedChannel) {
    EXPECT_FALSE(watchedChannel->prepared());
    EXPECT_FALSE(watchedChannel->responsesEncrypted());
    ++releaseCount;
  }
  __real_pair_cipher_free(cipher);
}
TEST(PairCipherBundle, ClearsOwnedStateBeforeCipherRelease) {
  EncryptedChannel channel;
  const std::array<uint8_t,32> key{};
  ASSERT_TRUE(channel.prepare(PAIR_SERVER_HOMEKIT,3,key,"","Control"));
  channel.enableEncryptedResponses();
  watchedChannel = &channel; releaseCount = 0;
  channel.release();
  EXPECT_EQ(releaseCount,1);
  watchedChannel = nullptr;
}
TEST(PairCipherBundle, ReleaseIsSafeAfterOwnershipIsEmpty) {
  EncryptedChannel channel;
  const std::array<uint8_t,32> key{};
  ASSERT_TRUE(channel.prepare(PAIR_SERVER_HOMEKIT,3,key,"","Control"));
  watchedChannel = &channel; releaseCount = 0;
  channel.release(); channel.release();
  EXPECT_EQ(releaseCount,1);
  watchedChannel = nullptr;
  EncryptedChannel empty; empty.release();
}

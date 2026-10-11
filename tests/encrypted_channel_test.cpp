#include <cstddef>
#include <cstdint>
#include <sys/types.h>
extern "C" {
#include "pair.h"
}
#include <gtest/gtest.h>
#include <array>
#include <memory>
#include <cstdlib>
#include <vector>

TEST(PairCipherFailure, DecryptFailureClearsReleasedOutputBeforeCallerCleanup) {
  const std::array<uint8_t,32> key{};
  std::unique_ptr<pair_cipher_context,decltype(&pair_cipher_free)> sender(
      pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL,0,key.data(),key.size(),""),pair_cipher_free);
  std::unique_ptr<pair_cipher_context,decltype(&pair_cipher_free)> receiver(
      pair_cipher_new(PAIR_SERVER_HOMEKIT,3,key.data(),key.size(),""),pair_cipher_free);
  ASSERT_TRUE(sender); ASSERT_TRUE(receiver);
  const std::array<uint8_t,4> message{1,2,3,4};
  uint8_t *encrypted = nullptr; size_t encryptedLength = 0;
  ASSERT_EQ(pair_encrypt(&encrypted,&encryptedLength,message.data(),message.size(),sender.get()),4);
  std::unique_ptr<uint8_t,decltype(&std::free)> wire(encrypted,std::free);
  wire.get()[encryptedLength-1] ^= 1;
  uint8_t *plain = nullptr; size_t plainLength = 0;
  EXPECT_EQ(pair_decrypt(&plain,&plainLength,wire.get(),encryptedLength,receiver.get()),-1);
  EXPECT_EQ(plain,nullptr); EXPECT_EQ(plainLength,0u);
  std::free(plain);
}

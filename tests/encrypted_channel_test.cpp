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
#include "transport/encrypted_channel.hpp"
#include <deque>
#include <algorithm>

class QuietCipherDiagnostics final : public CipherDiagnostics {
public:
  void decryptFailed(std::ptrdiff_t) override { ++failures; }
  void encryptFailed(std::string_view) override { ++failures; }
  int failures = 0;
};
class MemoryCipherIo final : public EncryptedByteIo {
public:
  std::ptrdiff_t read(std::span<uint8_t> bytes) override {
    ++reads;
    if (chunks.empty()) return terminal;
    auto chunk = std::move(chunks.front()); chunks.pop_front();
    EXPECT_LE(chunk.size(),bytes.size());
    std::copy(chunk.begin(),chunk.end(),bytes.begin());
    return chunk.size();
  }
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    ++writes;
    if (writeResult <= 0) return writeResult;
    const auto count = std::min(bytes.size(),static_cast<size_t>(writeResult));
    output.insert(output.end(),bytes.begin(),bytes.begin()+count);
    return count;
  }
  std::deque<std::vector<uint8_t>> chunks;
  std::vector<uint8_t> output;
  int reads = 0, writes = 0;
  std::ptrdiff_t terminal = 0, writeResult = 4096;
};

class EncryptedChannelTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(sender.prepare(PAIR_CLIENT_HOMEKIT_NORMAL,0,key,"","Client"));
    ASSERT_TRUE(receiver.prepare(PAIR_SERVER_HOMEKIT,3,key,"","Control"));
  }
  std::vector<uint8_t> frame() {
    MemoryCipherIo io;
    EXPECT_EQ(sender.write(io,message,diagnostics),message.size());
    return io.output;
  }
  std::array<uint8_t,32> key{};
  const std::array<uint8_t,5> message{0x52,0,0x54,0x53,0x50};
  EncryptedChannel sender, receiver;
  QuietCipherDiagnostics diagnostics;
};

TEST_F(EncryptedChannelTest, RealHomeKitFrameReturnsBinaryPlaintextAndServesLeftoversWithoutIO) {
  MemoryCipherIo io; io.chunks.push_back(frame());
  std::array<uint8_t,2> first{};
  EXPECT_EQ(receiver.read(io,first,diagnostics),2);
  EXPECT_EQ(first,(std::array<uint8_t,2>{0x52,0}));
  std::array<uint8_t,8> rest{};
  EXPECT_EQ(receiver.read(io,rest,diagnostics),3);
  EXPECT_TRUE(std::equal(message.begin()+2,message.end(),rest.begin()));
  EXPECT_EQ(io.reads,1); EXPECT_EQ(diagnostics.failures,0);
}
TEST_F(EncryptedChannelTest, CompleteLengthPrefixCanArriveBeforeRemainingFrame) {
  const auto wire = frame(); MemoryCipherIo io;
  io.chunks.emplace_back(wire.begin(),wire.begin()+2);
  io.chunks.emplace_back(wire.begin()+2,wire.end());
  std::array<uint8_t,5> decoded{};
  EXPECT_EQ(receiver.read(io,decoded,diagnostics),5);
  EXPECT_EQ(decoded,message); EXPECT_EQ(io.reads,2);
}
TEST_F(EncryptedChannelTest, PartialFrameSurvivesEOFForLaterRead) {
  const auto wire = frame(); MemoryCipherIo io;
  io.chunks.emplace_back(wire.begin(),wire.begin()+2);
  std::array<uint8_t,5> decoded{};
  EXPECT_EQ(receiver.read(io,decoded,diagnostics),0);
  io.chunks.emplace_back(wire.begin()+2,wire.end());
  EXPECT_EQ(receiver.read(io,decoded,diagnostics),5);
  EXPECT_EQ(decoded,message);
}
TEST_F(EncryptedChannelTest, SingleByteLengthFragmentPreservesLegacyFailure) {
  const auto wire = frame(); MemoryCipherIo io;
  io.chunks.emplace_back(wire.begin(),wire.begin()+1);
  std::array<uint8_t,5> decoded{};
  EXPECT_EQ(receiver.read(io,decoded,diagnostics),-1);
  EXPECT_EQ(diagnostics.failures,1);
}
TEST_F(EncryptedChannelTest, ShortWritesCompleteCipherFrameBeforeReturningPlaintextCount) {
  MemoryCipherIo io; io.writeResult = 3;
  EXPECT_EQ(sender.write(io,message,diagnostics),5);
  EXPECT_EQ(io.output.size(),23u); EXPECT_EQ(io.writes,8);
  MemoryCipherIo input; input.chunks.push_back(io.output);
  std::array<uint8_t,5> decoded{};
  EXPECT_EQ(receiver.read(input,decoded,diagnostics),5); EXPECT_EQ(decoded,message);
}
TEST_F(EncryptedChannelTest, WriteZeroAndErrorAreForwardedWithoutRetry) {
  for (auto result : {0,-1}) {
    MemoryCipherIo io; io.writeResult = result;
    EXPECT_EQ(sender.write(io,message,diagnostics),result); EXPECT_EQ(io.writes,1);
  }
}
TEST_F(EncryptedChannelTest, FailedAuthenticationReportsFailureAndReleasesSafely) {
  auto wire = frame(); wire.back() ^= 1;
  MemoryCipherIo io; io.chunks.push_back(wire);
  std::array<uint8_t,5> decoded{};
  EXPECT_EQ(receiver.read(io,decoded,diagnostics),-1);
  EXPECT_EQ(diagnostics.failures,1);
  receiver.release(); receiver.release();
}
TEST_F(EncryptedChannelTest, ReleaseClearsResponseActivationAndPreparedState) {
  EXPECT_FALSE(receiver.responsesEncrypted()); receiver.enableEncryptedResponses();
  EXPECT_TRUE(receiver.responsesEncrypted()); receiver.release();
  EXPECT_FALSE(receiver.prepared()); EXPECT_FALSE(receiver.responsesEncrypted());
}

TEST(EncryptedChannel, EmptyReleaseIsRepeatable) {
  EncryptedChannel channel;
  EXPECT_FALSE(channel.prepared());
  EXPECT_FALSE(channel.responsesEncrypted());
  channel.release();
  channel.release();
  EXPECT_FALSE(channel.prepared());
}

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

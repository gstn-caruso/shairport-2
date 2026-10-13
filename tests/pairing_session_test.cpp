#include "session/pairing_session.hpp"
#include <gtest/gtest.h>
#include <gcrypt.h>
#include <array>
#include <cstdlib>

static int failedCipherChannel = -1, setupCreations = 0, verifyCreations = 0;
static std::vector<int> preparedChannels;
extern "C" pair_cipher_context *__real_pair_cipher_new(pair_type,int,const uint8_t *,size_t,const char *);
extern "C" pair_cipher_context *__wrap_pair_cipher_new(pair_type type,int channel,const uint8_t *key,size_t length,const char *salt) {
  if (type == PAIR_SERVER_HOMEKIT) {
    preparedChannels.push_back(channel);
    if (channel == failedCipherChannel) return nullptr;
  }
  return __real_pair_cipher_new(type,channel,key,length,salt);
}
extern "C" pair_setup_context *__real_pair_setup_new(pair_type,const char *,pair_cb,void *,const char *);
extern "C" pair_setup_context *__wrap_pair_setup_new(pair_type type,const char *pin,pair_cb callback,void *arg,const char *id) {
  if (type == PAIR_SERVER_HOMEKIT) ++setupCreations;
  return __real_pair_setup_new(type,pin,callback,arg,id);
}
extern "C" pair_verify_context *__real_pair_verify_new(pair_type,const char *,pair_cb,void *,const char *);
extern "C" pair_verify_context *__wrap_pair_verify_new(pair_type type,const char *keys,pair_cb callback,void *arg,const char *id) {
  if (type == PAIR_SERVER_HOMEKIT) ++verifyCreations;
  return __real_pair_verify_new(type,keys,callback,arg,id);
}

class PairingSessionNative : public testing::Test {
protected:
  PairingSession server;
  std::unique_ptr<pair_setup_context,decltype(&pair_setup_free)> client{nullptr,pair_setup_free};
  pair_result *clientResult = nullptr;
  void SetUp() override {
    ASSERT_NE(gcry_check_version(nullptr),nullptr);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED,0);
    failedCipherChannel = -1; setupCreations = verifyCreations = 0; preparedChannels.clear();
  }
  PairingReply exchange(bool transient) {
    server.configure(std::make_shared<const PairingOptions>(PairingOptions{"00:11:22:33:44:55",transient ? std::nullopt : std::optional<std::string>("1234")}));
    client.reset(pair_setup_new(transient ? PAIR_CLIENT_HOMEKIT_TRANSIENT : PAIR_CLIENT_HOMEKIT_NORMAL,
        transient ? "3939" : "1234",nullptr,nullptr,"ordinary-client"));
    EXPECT_TRUE(client);
    PairingReply reply;
    for (int stage = 1; stage <= (transient ? 2 : 3); ++stage) {
      size_t length = 0;
      uint8_t *bytes = stage == 1 ? pair_setup_request1(&length,client.get())
                      : stage == 2 ? pair_setup_request2(&length,client.get())
                                   : pair_setup_request3(&length,client.get());
      std::unique_ptr<uint8_t,decltype(&std::free)> owned(bytes,std::free);
      EXPECT_NE(bytes,nullptr);
      reply = server.setup({bytes,length}); EXPECT_EQ(reply.status,200); EXPECT_TRUE(reply.bodyPresent);
      EXPECT_EQ(stage == 1 ? pair_setup_response1(client.get(),reply.body.data(),reply.body.size())
                 : stage == 2 ? pair_setup_response2(client.get(),reply.body.data(),reply.body.size())
                              : pair_setup_response3(client.get(),reply.body.data(),reply.body.size()),0);
    }
    EXPECT_EQ(pair_setup_result(nullptr,&clientResult,client.get()),0);
    return reply;
  }
  void proveChannel(EncryptedChannel &channel,int clientChannel,const char *salt) {
    struct Output final : EncryptedByteIo {
      std::vector<uint8_t> bytes;
      std::ptrdiff_t read(std::span<uint8_t>) override { return -1; }
      std::ptrdiff_t write(std::span<const uint8_t> input) override { bytes.assign(input.begin(),input.end()); return input.size(); }
    } io;
    struct Diagnostics final : CipherDiagnostics {
      void decryptFailed(std::ptrdiff_t) override { ADD_FAILURE(); }
      void encryptFailed(std::string_view) override { ADD_FAILURE(); }
    } diagnostics;
    ASSERT_NE(clientResult,nullptr);
    std::unique_ptr<pair_cipher_context,decltype(&pair_cipher_free)> receiver(
        pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL,clientChannel,clientResult->shared_secret,clientResult->shared_secret_len,salt),pair_cipher_free);
    ASSERT_TRUE(receiver);
    const std::array<uint8_t,4> body{'a',0,'b','c'};
    ASSERT_EQ(channel.write(io,body,diagnostics),4);
    uint8_t *plaintext = nullptr; size_t length = 0;
    ASSERT_EQ(pair_decrypt(&plaintext,&length,io.bytes.data(),io.bytes.size(),receiver.get()),io.bytes.size());
    std::unique_ptr<uint8_t,decltype(&std::free)> owned(plaintext,std::free);
    EXPECT_EQ(std::vector<uint8_t>(plaintext,plaintext+length),(std::vector<uint8_t>(body.begin(),body.end())));
  }
};

TEST_F(PairingSessionNative, FruitSetupReturnsAnOwnedBinaryPlistAfterContextRelease) {
  client.reset(pair_setup_new(PAIR_CLIENT_FRUIT, "1234", nullptr, nullptr, "fruit-client"));
  ASSERT_TRUE(client);
  size_t length = 0;
  std::unique_ptr<uint8_t, decltype(&std::free)> request(
      pair_setup_request1(&length, client.get()), std::free);
  ASSERT_TRUE(request);
  ASSERT_GT(length, 8);

  client.reset();

  EXPECT_EQ(std::string_view(reinterpret_cast<const char *>(request.get()), 8), "bplist00");
}

TEST_F(PairingSessionNative, TransientSetupPreparesControlThenEventAndOwnsReplyBytes) {
  auto reply = exchange(true); auto retained = reply.body;
  ASSERT_TRUE(server.controlChannel().prepared()); ASSERT_TRUE(server.eventChannel().prepared());
  EXPECT_EQ(preparedChannels,(std::vector<int>{3,4}));
  EXPECT_FALSE(server.controlChannel().responsesEncrypted());
  proveChannel(server.controlChannel(),0,""); proveChannel(server.eventChannel(),1,"");
  server.release(); EXPECT_EQ(reply.body,retained);
  EXPECT_FALSE(server.controlChannel().prepared()); EXPECT_FALSE(server.eventChannel().prepared());
  EXPECT_EQ(server.prepareDataChannel(1),DataPreparation::noSetupSecret);
}
TEST_F(PairingSessionNative, DataPreparationUsesCompletedSetupSecretAndDecimalSeed) {
  EXPECT_EQ(server.prepareDataChannel(42),DataPreparation::noSetupSecret);
  exchange(true);
  ASSERT_EQ(server.prepareDataChannel(18446744073709551615ULL),DataPreparation::prepared);
  proveChannel(server.dataChannel(),2,"18446744073709551615");
}
TEST_F(PairingSessionNative, CorrectPinSetupAndLegitimateVerificationKeepChannelsInactive) {
  exchange(false);
  EXPECT_FALSE(server.controlChannel().prepared()); EXPECT_FALSE(server.eventChannel().prepared());
  const char *keys = nullptr; ASSERT_EQ(pair_setup_result(&keys,nullptr,client.get()),0); ASSERT_NE(keys,nullptr);
  std::unique_ptr<pair_verify_context,decltype(&pair_verify_free)> verifier(
      pair_verify_new(PAIR_CLIENT_HOMEKIT_NORMAL,keys,nullptr,nullptr,"ordinary-client"),pair_verify_free);
  ASSERT_TRUE(verifier);
  for (int stage : {1,2}) {
    size_t length = 0;
    uint8_t *request = stage == 1 ? pair_verify_request1(&length,verifier.get()) : pair_verify_request2(&length,verifier.get());
    std::unique_ptr<uint8_t,decltype(&std::free)> owned(request,std::free); ASSERT_NE(request,nullptr);
    auto reply = server.verify({request,length}); ASSERT_EQ(reply.status,200); ASSERT_TRUE(reply.bodyPresent);
    EXPECT_EQ(stage == 1 ? pair_verify_response1(verifier.get(),reply.body.data(),reply.body.size())
                          : pair_verify_response2(verifier.get(),reply.body.data(),reply.body.size()),0);
  }
  EXPECT_EQ(verifyCreations,1); EXPECT_FALSE(server.controlChannel().prepared());
  EXPECT_FALSE(server.eventChannel().prepared());
}
TEST_F(PairingSessionNative, RepeatedMalformedCallsReuseNativeContexts) {
  server.configure(std::make_shared<const PairingOptions>(PairingOptions{"00:11:22:33:44:55",std::nullopt}));
  const std::array<uint8_t,3> invalid{6,1,255};
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(server.setup(invalid).status,470); EXPECT_EQ(server.verify(invalid).status,470);
  }
  EXPECT_EQ(setupCreations,1); EXPECT_EQ(verifyCreations,1);
}
TEST_F(PairingSessionNative, ControlFailureRetainsSuccessfulSetupReplyWithoutPreparingEvent) {
  failedCipherChannel = 3; const auto reply = exchange(true);
  EXPECT_EQ(reply.status,200); EXPECT_TRUE(reply.bodyPresent);
  EXPECT_EQ(reply.diagnostic,PairingDiagnostic::controlCipherFailed);
  EXPECT_EQ(preparedChannels,(std::vector<int>{3}));
  EXPECT_FALSE(server.controlChannel().prepared()); EXPECT_FALSE(server.eventChannel().prepared());
}
TEST_F(PairingSessionNative, EventFailureRetainsPreparedControlAndSuccessfulSetupReply) {
  failedCipherChannel = 4; const auto reply = exchange(true);
  EXPECT_EQ(reply.status,200); EXPECT_TRUE(reply.bodyPresent);
  EXPECT_EQ(reply.diagnostic,PairingDiagnostic::eventCipherFailed);
  EXPECT_EQ(preparedChannels,(std::vector<int>{3,4}));
  EXPECT_TRUE(server.controlChannel().prepared()); EXPECT_FALSE(server.eventChannel().prepared());
}
TEST_F(PairingSessionNative, DataCipherFailureLeavesControlAndEventUsable) {
  exchange(true); failedCipherChannel = 5;
  EXPECT_EQ(server.prepareDataChannel(42),DataPreparation::cipherFailed);
  EXPECT_FALSE(server.dataChannel().prepared());
  proveChannel(server.controlChannel(),0,""); proveChannel(server.eventChannel(),1,"");
}

TEST(PairingSession, UnconfiguredOwnerIsExplicitlyInertAndReleaseIsRepeatable) {
  PairingSession session;
  EXPECT_EQ(session.setup({}).status,500);
  EXPECT_EQ(session.verify({}).status,500);
  EXPECT_EQ(session.prepareDataChannel(42),DataPreparation::noSetupSecret);
  EXPECT_FALSE(session.controlChannel().prepared());
  session.release(); session.release();
  EXPECT_FALSE(session.eventChannel().prepared());
}

#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include "pairing_test_directory.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <cstdlib>
#include <gcrypt.h>

static bool failSetupCreation = false, failVerifyCreation = false;
extern "C" pair_setup_context *__real_pair_setup_new(pair_type,const char *,pair_cb,void *,const char *);
extern "C" pair_setup_context *__wrap_pair_setup_new(pair_type type,const char *pin,pair_cb callback,void *arg,const char *id) {
  if (failSetupCreation) return nullptr;
  return __real_pair_setup_new(type,pin,callback,arg,id);
}
extern "C" pair_verify_context *__real_pair_verify_new(pair_type,const char *,pair_cb,void *,const char *);
extern "C" pair_verify_context *__wrap_pair_verify_new(pair_type type,const char *keys,pair_cb callback,void *arg,const char *id) {
  if (failVerifyCreation) return nullptr;
  return __real_pair_verify_new(type,keys,callback,arg,id);
}

class PairingSessionAdapter : public testing::Test {
protected:
  SessionState session{};
  void SetUp() override {
    ASSERT_NE(gcry_check_version(nullptr),nullptr);
    gcry_control(GCRYCTL_INITIALIZATION_FINISHED,0);
    session.pairingSession.configure(makeTestPairingOptions());
  }
  void TearDown() override {
    free(session.ap2_client_name); session.ap2_client_name = nullptr;
    failSetupCreation = failVerifyCreation = false;
  }
  RtspMessage dispatch(std::string_view path,std::span<const uint8_t> bytes) {
    RtspMessage request,response; request.request("POST",path);
    request.replaceBody({reinterpret_cast<const char *>(bytes.data()),bytes.size()});
    request.addHeader("X-Apple-Client-Name","ordinary-client");
    rtsp_dispatch_request(&session,&request,&response); return response;
  }
};
TEST_F(PairingSessionAdapter, NativeMalformedOperationsReturnAuthorizationRequired) {
  const std::array<uint8_t,3> invalid{6,1,255};
  EXPECT_EQ(dispatch("/pair-setup",invalid).responseCode(),470);
  EXPECT_EQ(dispatch("/pair-verify",invalid).responseCode(),470);
  EXPECT_STREQ(session.ap2_client_name,"ordinary-client");
}
TEST_F(PairingSessionAdapter, NativeContextCreationFailureReturnsInternalError) {
  const std::array<uint8_t,3> request{6,1,1};
  failSetupCreation = true;
  auto setup = dispatch("/pair-setup",request);
  EXPECT_EQ(setup.responseCode(),500); EXPECT_EQ(setup.bodyLength(),0);
  EXPECT_EQ(setup.headerValue("Content-Type"),nullptr);
  failSetupCreation = false; failVerifyCreation = true;
  EXPECT_EQ(dispatch("/pair-verify",request).responseCode(),500);
}
TEST_F(PairingSessionAdapter, RealTransientExchangeReturnsPlaintextSetupAndPreparedChannels) {
  std::unique_ptr<pair_setup_context,decltype(&pair_setup_free)> client(
      pair_setup_new(PAIR_CLIENT_HOMEKIT_TRANSIENT,"3939",nullptr,nullptr,"ordinary-client"),pair_setup_free);
  ASSERT_TRUE(client);
  for (uint8_t stage : {2,4}) {
    size_t requestLength = 0;
    uint8_t *request = stage == 2 ? pair_setup_request1(&requestLength,client.get())
                                  : pair_setup_request2(&requestLength,client.get());
    ASSERT_NE(request,nullptr);
    std::unique_ptr<uint8_t,decltype(&std::free)> owned(request,std::free);
    auto response = dispatch("/pair-setup",{owned.get(),requestLength});
    ASSERT_EQ(response.responseCode(),200);
    EXPECT_STREQ(response.headerValue("Content-Type"),"application/octet-stream");
    ASSERT_GE(response.bodyLength(),3u);
    EXPECT_EQ(response.bodyText().substr(0,3),std::string_view(reinterpret_cast<const char *>(std::array<uint8_t,3>{6,1,stage}.data()),3));
    const auto *bytes = reinterpret_cast<const uint8_t *>(response.bodyData());
    EXPECT_EQ(stage == 2 ? pair_setup_response1(client.get(),bytes,response.bodyLength())
                          : pair_setup_response2(client.get(),bytes,response.bodyLength()),0);
  }
  pair_result *result = nullptr; ASSERT_EQ(pair_setup_result(nullptr,&result,client.get()),0);
  ASSERT_GT(result->shared_secret_len,0u);
  EXPECT_TRUE(session.pairingSession.controlChannel().prepared());
  EXPECT_TRUE(session.pairingSession.eventChannel().prepared());
  EXPECT_FALSE(session.pairingSession.controlChannel().responsesEncrypted());
}

TEST_F(PairingSessionAdapter, SeedBeforeSetupLeavesRemoteDataPortUnopened) {
  session.airplay_stream_category = remote_control_stream;
  session.connection_ip_family = AF_INET;
  strcpy(session.self_ip_string,"127.0.0.1");
  auto setup = plist_new_dict(); auto streams = plist_new_array(); auto stream = plist_new_dict();
  plist_dict_set_item(stream,"seed",plist_new_uint(42));
  plist_array_append_item(streams,stream); plist_dict_set_item(setup,"streams",streams);
  RtspMessage request,response; request.request("SETUP"); replaceBodyWithPlist(request,setup); plist_free(setup);
  rtsp_dispatch_request(&session,&request,&response);
  EXPECT_EQ(response.responseCode(),200); EXPECT_EQ(session.local_data_port,0);
  EXPECT_FALSE(session.pairingSession.dataChannel().prepared());
}

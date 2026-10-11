#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include <gtest/gtest.h>
#include <array>
#include <vector>
#include <string>

namespace {
using Bytes = std::vector<uint8_t>;
Bytes identifier(std::string_view id) {
  Bytes bytes{6,1,1,1,static_cast<uint8_t>(id.size())};
  bytes.insert(bytes.end(),id.begin(),id.end()); return bytes;
}
Bytes addRequest(std::string_view id,uint8_t value) {
  auto bytes = identifier(id); bytes.insert(bytes.end(),{3,32});
  bytes.insert(bytes.end(),32,value); return bytes;
}
void appendRecord(Bytes &bytes,std::string_view id,uint8_t value) {
  bytes.insert(bytes.end(),{1,static_cast<uint8_t>(id.size())});
  bytes.insert(bytes.end(),id.begin(),id.end()); bytes.insert(bytes.end(),{3,32});
  bytes.insert(bytes.end(),32,value); bytes.insert(bytes.end(),{11,1,1});
}
class PairingAdapter : public testing::Test {
protected:
  SessionState session{};
  std::vector<std::string> added;
  void SetUp() override { session.fd = -1; }
  void TearDown() override {
    for (const auto &id : added) dispatch("/pair-remove",identifier(id));
    free(session.ap2_client_name); session.ap2_client_name = nullptr;
  }
  RtspMessage dispatch(std::string_view path,const Bytes &bytes,std::string_view client = {}) {
    RtspMessage request,response; request.request("POST",path);
    request.replaceBody({reinterpret_cast<const char *>(bytes.data()),bytes.size()});
    if (!client.empty()) request.addHeader("X-Apple-Client-Name",client);
    rtsp_dispatch_request(&session,&request,&response); return response;
  }
  static Bytes body(const RtspMessage &response) {
    const auto *begin = reinterpret_cast<const uint8_t *>(response.bodyData());
    return response.bodyLength() ? Bytes(begin,begin+response.bodyLength()) : Bytes{};
  }
  void add(std::string_view id,uint8_t value) {
    added.emplace_back(id);
    auto response = dispatch("/pair-add",addRequest(id,value));
    EXPECT_EQ(response.responseCode(),200); EXPECT_EQ(body(response),(Bytes{6,1,2}));
    EXPECT_STREQ(response.headerValue("Content-Type"),"application/octet-stream");
  }
};

TEST_F(PairingAdapter, EmptyListIgnoresRequestBytesAndRemembersClientName) {
  auto response = dispatch("/pair-list",{255},"ordinary-client");
  EXPECT_EQ(response.responseCode(),200); EXPECT_EQ(body(response),(Bytes{6,1,2}));
  EXPECT_STREQ(session.ap2_client_name,"ordinary-client");
  EXPECT_STREQ(response.headerValue("Content-Type"),"application/octet-stream");
}
TEST_F(PairingAdapter, ValidRecordsListNewestFirstAndUpdateWithoutMoving) {
  add("alpha",1); add("beta",2); add("alpha",3);
  Bytes expected{6,1,2}; appendRecord(expected,"beta",2);
  expected.insert(expected.end(),{255,0}); appendRecord(expected,"alpha",3);
  EXPECT_EQ(body(dispatch("/pair-list",{})),expected);
  EXPECT_EQ(body(dispatch("/pair-remove",identifier("beta"))),(Bytes{6,1,2}));
  expected = {6,1,2}; appendRecord(expected,"alpha",3);
  EXPECT_EQ(body(dispatch("/pair-list",{})),expected);
  EXPECT_EQ(dispatch("/pair-remove",identifier("absent")).responseCode(),200);
}
TEST_F(PairingAdapter, IdentifierBoundariesAndNullPrefixRemainCompatible) {
  add(std::string(63,'i'),7);
  EXPECT_EQ(dispatch("/pair-add",addRequest(std::string(64,'j'),8)).responseCode(),451);
  const std::string embedded("prefix\0suffix",13);
  add(embedded,9); added.emplace_back("prefix");
  Bytes expected{6,1,2}; appendRecord(expected,"prefix",9);
  expected.insert(expected.end(),{255,0}); appendRecord(expected,std::string(63,'i'),7);
  EXPECT_EQ(body(dispatch("/pair-list",{})),expected);
}
TEST_F(PairingAdapter, TruncatedAddReturnsErrorWithoutMutation) {
  add("alpha",1);
  EXPECT_EQ(dispatch("/pair-add",{1,7,'x'}).responseCode(),451);
  Bytes expected{6,1,2}; appendRecord(expected,"alpha",1);
  EXPECT_EQ(body(dispatch("/pair-list",{})),expected);
}
}

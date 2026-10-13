#include "session/session_state.hpp"
#include "runtime/common.h"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include "platform/utilities/debug.h"
#include <gtest/gtest.h>
#include "protocol/ap2/ap2_event_receiver.h"
#include <netinet/in.h>
#include <unistd.h>

namespace {
class RtspDispatch : public testing::Test {
protected:
  rtsp_conn_info connection{};
  RtspMessage request;
  RtspMessage response;

  void SetUp() override { connection.thread = pthread_self(); }
  void dispatch() { rtsp_dispatch_request(&connection, &request, &response); }
  std::string captureDispatch(int verbosity) {
    const auto previous = debug_level();
    set_debug_level(verbosity);
    testing::internal::CaptureStderr();
    dispatch();
    auto output = testing::internal::GetCapturedStderr();
    set_debug_level(previous);
    return output;
  }
};

TEST_F(RtspDispatch, SettingVolumeAndProgressRemembersTheRequestedLevel) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("volume: -15.000000\r\nprogress: 0/44100/88200\r\n");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_DOUBLE_EQ(suggested_volume(&connection), -15.0);
}

TEST_F(RtspDispatch, GettingVolumeReturnsTheSessionLevelWithSixDecimalPlaces) {
  connection.volumeControl.rememberLevel(AirPlayVolume{-15.0});
  request.request("GET_PARAMETER");
  request.replaceBody("volume\r\n");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(response.bodyText(), "\r\nvolume: -15.000000\r\n");
}

TEST_F(RtspDispatch, GettingVolumeReturnsTheLevelSetByThePreviousRequest) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("volume: -15.000000\r\nprogress: 0/44100/88200\r\n");
  dispatch();
  ASSERT_EQ(response.responseCode(), 200);
  request = RtspMessage{};
  response = RtspMessage{};
  request.request("GET_PARAMETER");
  request.replaceBody("volume\r\n");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(response.bodyText(), "\r\nvolume: -15.000000\r\n");
}

struct MethodResponse {
  const char *method;
  int status;
};

TEST_F(RtspDispatch, EightByteVolumePrefixAcceptsNonTerminatorSuffix) {
  connection.volumeControl.rememberLevel(AirPlayVolume{-12.5});
  request.request("GET_PARAMETER");
  request.replaceBody("volumeXX");
  dispatch();
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(response.bodyText(), "\r\nvolume: -12.500000\r\n");
}

TEST_F(RtspDispatch, OtherVolumeRequestLengthsReturnAnEmptySuccess) {
  for (const auto body : {"volume", "volume\n", "volume\r\nextra", "otherXXX"}) {
    SCOPED_TRACE(body);
    request = RtspMessage{};
    response = RtspMessage{};
    request.request("GET_PARAMETER");
    request.replaceBody(body);
    dispatch();
    EXPECT_EQ(response.responseCode(), 200);
    EXPECT_TRUE(response.bodyText().empty());
  }
}

TEST_F(RtspDispatch, SuffixedTextTypeProcessesEveryVolumeAndKeepsTheLast) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "text/parameters; charset=utf-8");
  request.replaceBody("volume: -20\r\nprogress: 0/1/2\r\nvolume: -10\r\n");
  dispatch();
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_DOUBLE_EQ(suggested_volume(&connection), -10);
}

TEST_F(RtspDispatch, SuffixedMetadataTypePreservesContainerValidation) {
  const char complete[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  const char incomplete[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  for (const auto body : {std::string_view(complete, sizeof(complete)),
                          std::string_view(incomplete, sizeof(incomplete))}) {
    SCOPED_TRACE(body.size());
    request = RtspMessage{};
    response = RtspMessage{};
    request.request("SET_PARAMETER");
    request.addHeader("Content-Type", "application/x-dmap-tagged; suffix=accepted");
    request.replaceBody(body);
    dispatch();
    EXPECT_EQ(response.responseCode(), body.size() == sizeof(complete) ? 200 : 400);
  }
}

TEST_F(RtspDispatch, ImageUnknownAndMissingContentTypesLeaveVolumeAlone) {
  connection.volumeControl.rememberLevel(AirPlayVolume{-20});
  for (const auto type : {"image/jpeg", "application/unknown", ""}) {
    SCOPED_TRACE(type);
    request = RtspMessage{};
    response = RtspMessage{};
    request.request("SET_PARAMETER");
    if (*type) request.addHeader("Content-Type", type);
    request.replaceBody("volume: -10\r\n");
    dispatch();
    EXPECT_EQ(response.responseCode(), 200);
    EXPECT_DOUBLE_EQ(suggested_volume(&connection), -20);
  }
}

void PrintTo(const MethodResponse &method, std::ostream *output) {
  *output << method.method << " -> " << method.status;
}

class RtspMethodResponse : public RtspDispatch,
                           public testing::WithParamInterface<MethodResponse> {};

TEST_P(RtspMethodResponse, RespondsWithoutPriorRequests) {
  request.request(GetParam().method, "/unsupported");

  dispatch();

  EXPECT_EQ(response.responseCode(), GetParam().status);
}

INSTANTIATE_TEST_SUITE_P(Methods, RtspMethodResponse, testing::Values(
  MethodResponse{"RECORD", 200}, MethodResponse{"TEARDOWN", 200},
  MethodResponse{"GET", 501}, MethodResponse{"POST", 501}, MethodResponse{"FLUSH", 451},
  MethodResponse{"ANNOUNCE", 501}, MethodResponse{"PAUSE", 501},
  MethodResponse{"UNSUPPORTED", 501}
), [](const auto &info) { return info.param.method; });

TEST_F(RtspDispatch, OptionsAdvertisesAirplay2MethodsWithoutLegacyAnnounce) {
  request.request("OPTIONS");

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
  const char *methods = response.headerValue("Public");
  ASSERT_NE(methods, nullptr);
  const std::string_view advertised(methods);
  EXPECT_EQ(advertised.find("ANNOUNCE"), std::string_view::npos);
  EXPECT_NE(advertised.find("FLUSHBUFFERED"), std::string_view::npos);
  EXPECT_NE(advertised.find("GET_PARAMETER"), std::string_view::npos);
}

TEST_F(RtspDispatch, SetupRejectsNtpTiming) {
  request.request("SETUP");
  plist_t setup = plist_new_dict();
  plist_dict_set_item(setup, "timingProtocol", plist_new_string("NTP"));
  replaceBodyWithPlist(request, setup);
  plist_free(setup);

  dispatch();

  EXPECT_EQ(response.responseCode(), 400);
}

TEST_F(RtspDispatch, RemoteControlSetupAdvertisesOwnedPortAndStopReleasesIt) {
  connection.connection_ip_family = AF_INET;
  strcpy(connection.self_ip_string,"127.0.0.1");
  request.request("SETUP");
  auto setup = plist_new_dict();
  plist_dict_set_item(setup,"timingProtocol",plist_new_string("None"));
  plist_dict_set_item(setup,"isRemoteControlOnly",plist_new_bool(1));
  replaceBodyWithPlist(request,setup); plist_free(setup);
  dispatch();
  ASSERT_EQ(response.responseCode(),200);
  auto result = plistFromMessageBody(response); ASSERT_NE(result,nullptr);
  uint64_t port = 0;
  plist_get_uint_val(plist_dict_get_item(result,"eventPort"),&port); plist_free(result);
  ASSERT_GT(port,0u); EXPECT_EQ(port,connection.local_event_port);
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
  const int candidate = socket(AF_INET,SOCK_STREAM,0); ASSERT_GE(candidate,0);
  EXPECT_EQ(bind(candidate,reinterpret_cast<sockaddr *>(&address),sizeof(address)),-1);
  EXPECT_EQ(errno,EADDRINUSE);
  stop_ap2_event_worker(&connection);
  EXPECT_EQ(bind(candidate,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  close(candidate);
  connection.sessionPlist = nullptr;
}

TEST_F(RtspDispatch, MetadataRejectsAnIncompleteContainer) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char metadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  request.replaceBody({metadata, sizeof(metadata)});

  dispatch();

  EXPECT_EQ(response.responseCode(), 400);
}

TEST_F(RtspDispatch, MetadataAcceptsACompleteContainer) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char metadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  request.replaceBody({metadata, sizeof(metadata)});

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
}

TEST_F(RtspDispatch, MetadataRejectionWarnsAtNormalVerbosityWithoutPayloadOrCredentials) {
  connection.connection_number = 42;
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  request.addHeader("Authorization", "credential-marker");
  request.replaceBody("payload-marker");
  const auto output = captureDispatch(0);
  EXPECT_EQ(response.responseCode(), 400);
  EXPECT_NE(output.find("Connection 42: SET_PARAMETER rejected invalid tagged metadata framing"),
            std::string::npos);
  EXPECT_NE(output.find("body bytes: 14, response status: 400"), std::string::npos);
  EXPECT_EQ(output.find("payload-marker"), std::string::npos);
  EXPECT_EQ(output.find("credential-marker"), std::string::npos);
}

TEST_F(RtspDispatch, CompleteMetadataDoesNotEmitARejectionWarning) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char complete[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  request.replaceBody({complete, sizeof(complete)});
  EXPECT_EQ(captureDispatch(0).find("rejected invalid tagged metadata"), std::string::npos);
  EXPECT_EQ(response.responseCode(), 200);
}

TEST_F(RtspDispatch, UnknownParameterDiagnosticsSummarizeWithoutDumpingValuesOrHeaders) {
  for (auto type : {"text/parameters", "application/type-marker"}) {
    request = RtspMessage{};
    response = RtspMessage{};
    request.request("SET_PARAMETER");
    request.addHeader("Content-Type", type);
    request.addHeader("Authorization", "credential-marker");
    request.replaceBody("payload-marker\r\n");
    const auto output = captureDispatch(1);
    EXPECT_EQ(response.responseCode(), 200);
    EXPECT_NE(output.find("SET_PARAMETER"), std::string::npos);
    EXPECT_NE(output.find("body bytes: 16"), std::string::npos);
    EXPECT_EQ(output.find("payload-marker"), std::string::npos);
    EXPECT_EQ(output.find("credential-marker"), std::string::npos);
    EXPECT_EQ(output.find("type-marker"), std::string::npos);
  }
}

TEST_F(RtspDispatch, CompleteMetadataIsAcceptedAfterAnIncompleteRequest) {
  request.request("SET_PARAMETER");
  request.addHeader("Content-Type", "application/x-dmap-tagged");
  const char incomplete[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  request.replaceBody({incomplete, sizeof(incomplete)});
  dispatch();
  ASSERT_EQ(response.responseCode(), 400);
  const char complete[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  request.replaceBody({complete, sizeof(complete)});
  response = RtspMessage{};

  dispatch();

  EXPECT_EQ(response.responseCode(), 200);
}
}

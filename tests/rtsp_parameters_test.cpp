#include "protocol/rtsp/rtsp_message.hpp"
#include "volume/volume_quantities.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

import receiver.protocol.rtsp.parameters;

namespace {
class SessionVolume : public ParameterVolumePort {
public:
  AirPlayVolume current{-24};
  std::vector<AirPlayVolume> accepted;
  AirPlayVolume suggestedVolume() override { return current; }
  void acceptVolume(AirPlayVolume volume) override {
    accepted.push_back(volume);
    current = volume;
  }
};

class RtspParameters : public testing::Test {
protected:
  SessionVolume volume;
  RtspParameterHandler handler{volume};
  RtspMessage request, response;
};

TEST_F(RtspParameters, EmptyGetReturnsSuccessWithoutChangingTheBodyOrVolume) {
  response.replaceBody("existing");
  EXPECT_FALSE(handler.get(request, response));
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(response.bodyText(), "existing");
  EXPECT_TRUE(volume.accepted.empty());
}

TEST_F(RtspParameters, EightByteVolumePrefixReportsSixDecimalPlaces) {
  volume.current = AirPlayVolume{-12.5};
  for (const auto body : {"volume\r\n", "volumeXX"}) {
    SCOPED_TRACE(body);
    request.replaceBody(body);
    EXPECT_EQ(handler.get(request, response), AirPlayVolume{-12.5});
    EXPECT_EQ(response.responseCode(), 200);
    EXPECT_EQ(response.bodyText(), "\r\nvolume: -12.500000\r\n");
  }
}

TEST_F(RtspParameters, OtherGetBodiesReturnAnEmptySuccess) {
  for (const auto body : {"volume", "volume\n", "volume\r\nextra", "otherXXX"}) {
    SCOPED_TRACE(body);
    request.replaceBody(body);
    EXPECT_FALSE(handler.get(request, response));
    EXPECT_EQ(response.responseCode(), 200);
    EXPECT_TRUE(response.bodyText().empty());
  }
}

TEST_F(RtspParameters, TextSuffixAppliesVolumesInOrderAndReportsOnlyUnknownLines) {
  request.addHeader("Content-Type", "text/parameters; charset=utf-8");
  request.replaceBody("volume: -20\r\nprogress: 0/1/2\r\nunknown: value\r\nvolume: -10\r\n");
  const auto diagnostics = handler.set(request, response);
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(diagnostics.content, ParameterContent::text);
  EXPECT_EQ(diagnostics.unrecognizedParameters, (std::vector<std::string>{"unknown: value"}));
  EXPECT_EQ(volume.accepted, (std::vector{AirPlayVolume{-20}, AirPlayVolume{-10}}));
  EXPECT_EQ(volume.current, AirPlayVolume{-10});
}

TEST_F(RtspParameters, MalformedVolumeIsAcceptedAsZero) {
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("volume: not-a-number\r\n");
  handler.set(request, response);
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(volume.accepted, std::vector{AirPlayVolume{0}});
}

TEST_F(RtspParameters, MixedLineSeparatorsPreserveEmptyUnknownLines) {
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("\r\nvolume: -20\rprogress: 0/1/2\n\nunknown\r\nvolume: -10");
  const auto diagnostics = handler.set(request, response);
  EXPECT_EQ(diagnostics.unrecognizedParameters, (std::vector<std::string>{"", "", "unknown"}));
  EXPECT_EQ(volume.accepted, (std::vector{AirPlayVolume{-20}, AirPlayVolume{-10}}));
  EXPECT_EQ(response.responseCode(), 200);
}

TEST_F(RtspParameters, WireVolumeAcceptsWhitespaceAndTrailingText) {
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("volume:   -12.5remaining\r\n");
  handler.set(request, response);
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_EQ(volume.accepted, std::vector{AirPlayVolume{-12.5}});
}

TEST_F(RtspParameters, WireFloatRoundingPreservesDecimalAndExactMuteValues) {
  request.addHeader("Content-Type", "text/parameters");
  request.replaceBody("volume: -15.1234567\r\nvolume: -144.000001\r\n");
  handler.set(request, response);
  EXPECT_EQ(response.responseCode(), 200);
  ASSERT_EQ(volume.accepted.size(), 2u);
  EXPECT_DOUBLE_EQ(volume.accepted[0].value(), -15.123456954956055);
  EXPECT_DOUBLE_EQ(volume.accepted[1].value(), -144);
  EXPECT_TRUE(volume.current.isMute());
}

TEST_F(RtspParameters, EmptyTextAndMalformedPrefixesApplyNoVolumes) {
  request.addHeader("Content-Type", "text/parameters");
  EXPECT_TRUE(handler.set(request, response).unrecognizedParameters.empty());
  EXPECT_EQ(response.responseCode(), 200);
  request.replaceBody("volume:-10\r\nVolume: -5\r\n");
  EXPECT_EQ(handler.set(request, response).unrecognizedParameters,
            (std::vector<std::string>{"volume:-10", "Volume: -5"}));
  EXPECT_EQ(response.responseCode(), 200);
  EXPECT_TRUE(volume.accepted.empty());
}

TEST_F(RtspParameters, MetadataSuffixPreservesCompleteAndIncompleteStatuses) {
  request.addHeader("Content-Type", "application/x-dmap-tagged; suffix=accepted");
  const char complete[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  const char incomplete[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  request.replaceBody({complete, sizeof(complete)});
  EXPECT_EQ(handler.set(request, response).content, ParameterContent::metadata);
  EXPECT_EQ(response.responseCode(), 200);
  request.replaceBody({incomplete, sizeof(incomplete)});
  EXPECT_EQ(handler.set(request, response).content, ParameterContent::metadata);
  EXPECT_EQ(response.responseCode(), 400);
  EXPECT_TRUE(volume.accepted.empty());
}

TEST_F(RtspParameters, ImageUnknownAndMissingTypesApplyNoVolumes) {
  for (const auto type : {"image/jpeg", "application/unknown", ""}) {
    SCOPED_TRACE(type);
    request = RtspMessage{};
    if (*type) request.addHeader("Content-Type", type);
    request.replaceBody("volume: -10\r\n");
    const auto diagnostics = handler.set(request, response);
    EXPECT_EQ(diagnostics.content, *type == 'i' ? ParameterContent::image :
                                  *type ? ParameterContent::unknown : ParameterContent::missing);
    EXPECT_TRUE(diagnostics.unrecognizedParameters.empty());
    EXPECT_EQ(response.responseCode(), 200);
    EXPECT_TRUE(volume.accepted.empty());
    EXPECT_EQ(volume.current, AirPlayVolume{-24});
  }
}
}

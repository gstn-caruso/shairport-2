#include "protocol/rtsp/rtsp_message.hpp"
#include "protocol/rtsp/parameter_body.hpp"
#include "protocol/rtsp/tagged_metadata_body.hpp"
#include <gtest/gtest.h>
#include <string>

TEST(RtspMessage, RequestLinePreservesMethodAndPathWhileHeadersDetermineBodyLength) {
  RtspMessage message;
  ASSERT_EQ(message.readLine("  OPTIONS  /info  RTSP/1.0"), -1);
  EXPECT_STREQ(message.methodName(), "OPTIONS");
  EXPECT_STREQ(message.requestPath(), "/info");
  EXPECT_EQ(message.readLine("Content-Length: 3"), -1);
  EXPECT_EQ(message.readLine(""), 3);
}

TEST(RtspMessage, LongRequestTokensAreTruncatedToWireFieldLimits) {
  RtspMessage message;
  ASSERT_EQ(message.readLine(std::string(30, 'M') + " /" + std::string(300, 'p') + " HTTP/1.1"), -1);
  EXPECT_EQ(std::string_view(message.methodName()), std::string(15, 'M'));
  EXPECT_EQ(std::string_view(message.requestPath()), "/" + std::string(254, 'p'));
}


TEST(RtspMessage, HeaderLookupPreservesFirstCaseInsensitiveDuplicate) {
  RtspMessage message;
  EXPECT_TRUE(message.addHeader("CSeq", "first"));
  EXPECT_TRUE(message.addHeader("cseq", "second"));
  EXPECT_STREQ(message.headerValue("CSEQ"), "first");
}

TEST(RtspMessage, SixteenHeadersFitAndTheFollowingHeaderIsRejected) {
  RtspMessage message;
  for (int index = 0; index < 16; ++index) {
    SCOPED_TRACE(index);
    EXPECT_TRUE(message.addHeader("Repeated", "value"));
  }
  EXPECT_FALSE(message.addHeader("Overflow", "ignored"));
  EXPECT_EQ(message.headerValue("Overflow"), nullptr);
  EXPECT_EQ(message.headers().size(), 16);
}

TEST(RtspMessage, ParsedHeaderOverflowIsIgnoredWithoutRejectingTheRequest) {
  RtspMessage message;
  ASSERT_EQ(message.readLine("OPTIONS / RTSP/1.0"), -1);
  for (int index = 0; index < 16; ++index)
    EXPECT_EQ(message.readLine("Repeated: value"), -1);
  EXPECT_EQ(message.readLine("Content-Length: 42"), -1);
  EXPECT_EQ(message.readLine(""), 0);
  EXPECT_EQ(message.headerValue("Content-Length"), nullptr);
  EXPECT_EQ(message.headers().size(), 16);
}


TEST(RtspMessage, OwnedRequestBodyPreservesBorrowedBytesAndSupportsEmptyReplacement) {
  RtspMessage owned;
  EXPECT_EQ(owned.readLine("OPTIONS /info RTSP/1.0"), -1);
  EXPECT_TRUE(owned.requestsMethod("OPTIONS"));
  EXPECT_TRUE(owned.requestsPath("/info"));
  EXPECT_EQ(owned.readLine("Content-Length: 3"), -1);
  EXPECT_EQ(owned.readLine(""), 3);
  std::string borrowed("A\0B", 3);
  owned.replaceBody(borrowed);
  borrowed[0] = 'X';
  EXPECT_EQ(owned.bodyText(), std::string_view("A\0B", 3));
  EXPECT_EQ(owned.bodyData()[3], '\0');
  owned.replaceBody("");
  EXPECT_EQ(owned.bodyLength(), 0);
}

TEST(RtspMessage, EmptyResponseContainsAZeroContentLength) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  auto packet = framedResponse.responsePacket();
  ASSERT_TRUE(packet.has_value());
  EXPECT_EQ(*packet, "RTSP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n");
}

TEST(RtspMessage, ResponsePacketPreservesDuplicateHeadersAndBinaryBody) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  framedResponse.addHeader("CSeq", "1");
  framedResponse.addHeader("CSeq", "2");
  framedResponse.replaceBody(std::string_view("A\0B", 3));
  std::string duplicateHeaders = "RTSP/1.0 200 OK\r\nCSeq: 1\r\nCSeq: 2\r\nContent-Length: 3\r\n\r\n";
  duplicateHeaders.append("A\0B", 3);
  auto packet = framedResponse.responsePacket();
  ASSERT_TRUE(packet.has_value());
  EXPECT_EQ(*packet, duplicateHeaders);
}

TEST(RtspMessage, OversizedResponseBodyReturnsAFramingError) {
  RtspMessage framedResponse;
  framedResponse.respondWith(200);
  framedResponse.replaceBody(std::string(4096, 'x'));
  auto packet = framedResponse.responsePacket();
  ASSERT_FALSE(packet.has_value());
  EXPECT_EQ(packet.error(), RtspMessage::FramingError::bodyTooLong);
}

TEST(TaggedMetadataBody, CompleteMetadataReplacesAnIncompleteContainer) {
  const char invalidMetadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 20};
  EXPECT_FALSE(TaggedMetadataBody({invalidMetadata, sizeof(invalidMetadata)}).isComplete());
  const char validMetadata[] = {'m', 'l', 'i', 't', 0, 0, 0, 8, 'm', 'i', 'n', 'm', 0, 0, 0, 0};
  EXPECT_TRUE(TaggedMetadataBody({validMetadata, sizeof(validMetadata)}).isComplete());
}

TEST(ParameterBody, LinesPreserveVolumeAndProgressValues) {
  ParameterBody parameters("volume: -15.0\r\nprogress: 0/1/2\r\n");
  EXPECT_EQ(parameters.nextLine(), "volume: -15.0");
  EXPECT_EQ(parameters.nextLine(), "progress: 0/1/2");
  EXPECT_EQ(parameters.nextLine(), std::nullopt);
}

#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include <gtest/gtest.h>
#include <cerrno>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

class RequestSocket : public testing::Test {
protected:
  void SetUp() override {
    int sockets[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
    connection.fd = sockets[0];
    peer = sockets[1];
  }
  void TearDown() override {
    msg_free(&message);
    connection.pairingSession.controlChannel().release();
    close(peer);
  }
  void send(std::string_view bytes) {
    ASSERT_EQ(write(peer, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
  }
  rtsp_read_request_response receive() {
    errno = 0;
    return rtsp_read_request(&connection, &message);
  }
  SessionState connection{};
  int peer = -1;
  RtspMessage *message = nullptr;
};

TEST_F(RequestSocket, MixedDelimitersPreserveBinaryBodyAndCStringHeaderParsing) {
  std::string packet = "POST /feedback RTSP/1.0\rContent-Length: 3\nX: a";
  packet.append("\0ignored", 8);
  packet += "\r\n\r\n";
  packet.append("A\0B", 3);
  send(packet);
  ASSERT_EQ(receive(), rtsp_read_request_response_ok);
  ASSERT_NE(message, nullptr);
  EXPECT_TRUE(message->requestsMethod("POST"));
  EXPECT_TRUE(message->requestsPath("/feedback"));
  EXPECT_STREQ(message->headerValue("X"), "a");
  EXPECT_EQ(message->bodyText(), std::string_view("A\0B", 3));
}

TEST_F(RequestSocket, BufferedExcessIncludingNextRequestRemainsInBody) {
  const std::string excess = "abcOPTIONS /info RTSP/1.0\r\n\r\n";
  send("POST /feedback RTSP/1.0\r\nContent-Length: 1\r\n\r\n" + excess);
  ASSERT_EQ(receive(), rtsp_read_request_response_ok);
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->bodyText(), excess);
}

TEST_F(RequestSocket, HeaderEofClosesLocalDescriptorAndReleasesMessage) {
  shutdown(peer, SHUT_WR);
  EXPECT_EQ(receive(), rtsp_read_request_response_channel_closed);
  EXPECT_EQ(connection.fd, -1);
  EXPECT_EQ(message, nullptr);
}

TEST_F(RequestSocket, StaleTimeoutAtHeaderEofRequestsShutdownWithoutClosing) {
  shutdown(peer, SHUT_WR);
  errno = ETIMEDOUT;
  EXPECT_EQ(rtsp_read_request(&connection, &message), rtsp_read_request_response_immediate_shutdown_requested);
  EXPECT_GE(connection.fd, 0);
  EXPECT_EQ(message, nullptr);
}

TEST_F(RequestSocket, BodyEofLeavesDescriptorForSessionTeardown) {
  send("POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\nA");
  shutdown(peer, SHUT_WR);
  EXPECT_EQ(receive(), rtsp_read_request_response_channel_closed);
  EXPECT_GE(connection.fd, 0);
  EXPECT_EQ(message, nullptr);
}

TEST_F(RequestSocket, NegativeLengthContinuesParsingHeaderLines) {
  send("POST /feedback RTSP/1.0\r\nContent-Length: -1\r\n\r\nNotAHeader\r\n");
  EXPECT_EQ(receive(), rtsp_read_request_response_bad_packet);
  EXPECT_EQ(message, nullptr);
  EXPECT_GE(connection.fd, 0);
}

TEST_F(RequestSocket, FullHeaderWithoutDelimiterPerformsZeroLengthReadAndCloses) {
  send(std::string(4096, 'X'));
  EXPECT_EQ(receive(), rtsp_read_request_response_channel_closed);
  EXPECT_EQ(connection.fd, -1);
  EXPECT_EQ(message, nullptr);
}

TEST_F(RequestSocket, CipherAdapterDecryptsActualEncryptedRequest) {
  const std::array<uint8_t, 32> keyMaterial{};
  auto &bundle = connection.pairingSession.controlChannel();
  ASSERT_TRUE(bundle.prepare(PAIR_SERVER_HOMEKIT, 3, keyMaterial, "", "Control"));
  EXPECT_FALSE(bundle.responsesEncrypted());
  std::unique_ptr<pair_cipher_context, decltype(&pair_cipher_free)> sender(
      pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL, 0, keyMaterial.data(), keyMaterial.size(), ""),
      pair_cipher_free);
  ASSERT_NE(sender, nullptr);
  const std::string packet = "POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\nabc";
  uint8_t *encrypted = nullptr;
  size_t encryptedLength = 0;
  const auto consumed = pair_encrypt(&encrypted, &encryptedLength,
      reinterpret_cast<const uint8_t *>(packet.data()), packet.size(), sender.get());
  std::unique_ptr<uint8_t, decltype(&std::free)> bytes(encrypted, std::free);
  ASSERT_EQ(consumed, static_cast<ssize_t>(packet.size()));
  send(std::string_view(reinterpret_cast<const char *>(bytes.get()), encryptedLength));
  ASSERT_EQ(receive(), rtsp_read_request_response_ok);
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(message->bodyText(), "abc");
  EXPECT_TRUE(message->requestsPath("/feedback"));
  EXPECT_TRUE(bundle.responsesEncrypted());

  RtspMessage response;
  response.respondWith(200);
  const auto expected = response.responsePacket();
  ASSERT_TRUE(expected);
  ASSERT_EQ(msg_write_response(&connection,&response),0);
  std::array<uint8_t,4096> reply{};
  const auto count = read(peer,reply.data(),reply.size());
  ASSERT_GT(count,0);
  uint8_t *plain = nullptr; size_t plainLength = 0;
  ASSERT_EQ(pair_decrypt(&plain,&plainLength,reply.data(),count,sender.get()),count);
  std::unique_ptr<uint8_t,decltype(&std::free)> decoded(plain,std::free);
  EXPECT_EQ(std::string_view(reinterpret_cast<char *>(decoded.get()),plainLength),*expected);
}

TEST_F(RequestSocket, PreparedControlCipherKeepsPairSetupResponsePlaintext) {
  const std::array<uint8_t,32> key{};
  auto &channel = connection.pairingSession.controlChannel();
  ASSERT_TRUE(channel.prepare(PAIR_SERVER_HOMEKIT,3,key,"","Control"));
  RtspMessage response; response.respondWith(200);
  response.replaceBody(std::string_view("A\0B",3));
  const auto expected = response.responsePacket(); ASSERT_TRUE(expected);
  ASSERT_EQ(msg_write_response(&connection,&response),0);
  std::array<char,4096> reply{};
  const auto count = read(peer,reply.data(),reply.size()); ASSERT_GT(count,0);
  EXPECT_EQ(std::string_view(reply.data(),count),*expected);
  EXPECT_FALSE(channel.responsesEncrypted());
}

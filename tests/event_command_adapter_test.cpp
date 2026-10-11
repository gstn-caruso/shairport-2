#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/ap2/native_event_channel.hpp"
#include <gtest/gtest.h>
#include <array>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

ssize_t ap2_event_port_post_command(rtsp_conn_info *,plist_t);
TEST(NativeEventChannel, CloseOwnsAcceptedFdWithoutAccessingReleasedCipher) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  pair_cipher_bundle cipher{};
  auto channel = makeNativeEventChannel(sockets[0],cipher);
  EXPECT_NE(fcntl(sockets[0],F_GETFD),-1);
  cipher.release(); channel.reset();
  EXPECT_EQ(fcntl(sockets[0],F_GETFD),-1); close(sockets[1]);
}
TEST(EventCommandRuntime, BinaryPlistPostUsesRealEventCipherAcrossTwoTransactions) {
  int sockets[2]; ASSERT_EQ(socketpair(AF_UNIX,SOCK_STREAM,0,sockets),0);
  const timeval timeout{1,0};
  for (int fd : sockets) ASSERT_EQ(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout)),0);
  SessionState session{};
  pair_cipher_bundle peer{};
  const std::array<uint8_t,32> key{};
  auto &cipher = session.ap2_pairing_context.event_cipher_bundle;
  cipher.cipher_ctx = pair_cipher_new(PAIR_SERVER_HOMEKIT,4,key.data(),key.size(),"");
  peer.cipher_ctx = pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL,1,key.data(),key.size(),"");
  ASSERT_NE(cipher.cipher_ctx,nullptr); ASSERT_NE(peer.cipher_ctx,nullptr);
  ASSERT_EQ(pthread_mutex_init(&session.event_sender_mutex,nullptr),0);
  session.event_channel_fd = sockets[0];
  std::vector<std::vector<uint8_t>> received;
  const std::vector<std::vector<uint8_t>> replies{{'o',0,'k'},{'n','e','x','t'}};
  std::thread client([&] {
    for (const auto &reply : replies) {
      std::array<uint8_t,4096> bytes{};
      const auto count = read_encrypted(sockets[1],&peer,bytes.data(),bytes.size());
      EXPECT_GT(count,0); if (count <= 0) break;
      received.emplace_back(bytes.begin(),bytes.begin()+count);
      EXPECT_EQ(write_encrypted(sockets[1],&peer,reply.data(),reply.size()),static_cast<ssize_t>(reply.size()));
    }
  });
  const char data[]{0,1,static_cast<char>(0xff)};
  auto command = plist_new_data(data,sizeof(data));
  EXPECT_EQ(ap2_event_port_post_command(&session,command),3);
  EXPECT_EQ(ap2_event_port_post_command(&session,command),4);
  plist_free(command); client.join();
  const std::vector<uint8_t> binary{'b','p','l','i','s','t','0','0',0x43,0,1,0xff,8,
      0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,12};
  const std::string header = "POST /command RTSP/1.0\r\nContent-Length: 45\r\nContent-Type: application/x-apple-binary-plist\r\n\r\n";
  std::vector<uint8_t> expected(header.begin(),header.end()); expected.insert(expected.end(),binary.begin(),binary.end());
  ASSERT_EQ(received.size(),2u); EXPECT_EQ(received[0],expected); EXPECT_EQ(received[1],expected);
  close(sockets[0]); close(sockets[1]); pthread_mutex_destroy(&session.event_sender_mutex);
  cipher.release(); peer.release();
}

#include "buffered_block_fixture.hpp"
#include "session/session_state.hpp"
#include "protocol/rtp/rtp.h"
#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

struct RealtimeSubmission {
  uint32_t ssrc,timestamp;
  uint16_t sequence;
  int mute,gap;
  std::vector<uint8_t> bytes;
};
static std::mutex submittedMutex;
static std::condition_variable submittedChanged;
static std::vector<RealtimeSubmission> submitted;
extern "C" uint32_t __wrap_player_put_packet(uint32_t ssrc,seq_t sequence,uint32_t timestamp,
    uint8_t *data,size_t size,int mute,int32_t gap,rtsp_conn_info *) {
  {
    std::lock_guard lock(submittedMutex);
    std::vector<uint8_t> bytes;
    if (size != 0) bytes.assign(data,data+size);
    submitted.push_back({ssrc,timestamp,sequence,mute,gap,std::move(bytes)});
  }
  submittedChanged.notify_all();
  return 352;
}
void decipher_player_put_packet(uint8_t *,ssize_t,rtsp_conn_info *);

TEST(RealtimeAudioLegacy, GoldenStrippedPacketAuthenticatesAndSubmitsExactPlaintext) {
  submitted.clear();
  SessionState session{};
  session.session_key = const_cast<uint8_t *>(buffered_block_fixture::key.data());
  auto wire = buffered_block_fixture::encrypt();
  decipher_player_put_packet(wire.data()+2,wire.size()-2,&session);
  ASSERT_EQ(submitted.size(), 1u);
  EXPECT_EQ(submitted[0].ssrc, ALAC_44100_S16_2);
  EXPECT_EQ(submitted[0].sequence, 0xcdef);
  EXPECT_EQ(submitted[0].timestamp, 0x01020304u);
  EXPECT_EQ(submitted[0].mute, 0); EXPECT_EQ(submitted[0].gap, 0);
  EXPECT_EQ(submitted[0].bytes, (std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end())));
}
TEST(RealtimeAudioLegacy, FailedAuthenticationMustNotSubmitToPlayer) {
  submitted.clear();
  SessionState session{};
  session.session_key = const_cast<uint8_t *>(buffered_block_fixture::key.data());
  auto wire = buffered_block_fixture::encrypt(); wire[12] ^= 1;
  decipher_player_put_packet(wire.data()+2,wire.size()-2,&session);
  EXPECT_TRUE(submitted.empty());
}
TEST(RealtimeAudioLegacy, AuthenticatedEmptyControlPayloadStillSubmits) {
  submitted.clear();
  SessionState session{};
  session.session_key = const_cast<uint8_t *>(buffered_block_fixture::key.data());
  auto wire = buffered_block_fixture::encrypt(std::span<const uint8_t>{});
  decipher_player_put_packet(wire.data()+2,wire.size()-2,&session);
  ASSERT_EQ(submitted.size(),1u);
  EXPECT_TRUE(submitted[0].bytes.empty());
  EXPECT_EQ(submitted[0].sequence,0xcdef);
}
struct RealtimeRoute { bool control; const char *name; };
void PrintTo(const RealtimeRoute &route,std::ostream *output) { *output << route.name; }
class RealtimeIngressSockets : public testing::TestWithParam<RealtimeRoute> {};
TEST_P(RealtimeIngressSockets, AudioAndD6UseExactAuthenticatedBytesAndRejectFailedMacBeforeSubmission) {
  const bool control = GetParam().control;
  submitted.clear();
  SessionState session{};
  session.connection_number = 52;
  session.session_key = const_cast<uint8_t *>(buffered_block_fixture::key.data());
  const int socketFd = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(socketFd,0);
  sockaddr_in address{};
  address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(socketFd,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  socklen_t addressSize = sizeof(address);
  ASSERT_EQ(getsockname(socketFd,reinterpret_cast<sockaddr *>(&address),&addressSize),0);
  if (control) ASSERT_EQ(session.controlChannel.adopt(socketFd),0);
  else session.realtime_audio_socket = socketFd;
  const int client = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(client,0);
  ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,control ? rtp_ap2_control_receiver : rtp_realtime_audio_receiver,&session),0);
  for (unsigned index = 0; index < 3; ++index) {
    auto wire = buffered_block_fixture::encrypt();
    if (index == 1) wire[12] ^= 1;
    if (index == 2) { wire[2] = 0x12; wire[3] = 0x34; }
    std::vector<uint8_t> packet;
    if (control) {
      packet = {0x10,0xd6,0,0,0,0};
      packet.insert(packet.end(),wire.begin()+2,wire.end());
    } else packet = std::move(wire);
    EXPECT_EQ(send(client,packet.data(),packet.size(),MSG_NOSIGNAL),static_cast<ssize_t>(packet.size()));
  }
  bool arrived;
  {
    std::unique_lock lock(submittedMutex);
    arrived = submittedChanged.wait_for(lock,std::chrono::seconds(1),[] {
      return std::count_if(submitted.begin(),submitted.end(),[](const auto &packet) { return !packet.bytes.empty(); }) == 2;
    });
  }
  EXPECT_EQ(pthread_cancel(worker),0);
  EXPECT_EQ(pthread_join(worker,nullptr),0);
  close(client);
  ASSERT_TRUE(arrived);
  ASSERT_EQ(submitted.size(),2u);
  EXPECT_EQ(submitted[0].sequence,0xcdef);
  EXPECT_EQ(submitted[1].sequence,0x1234);
  for (const auto &packet : submitted) {
    EXPECT_EQ(packet.ssrc,ALAC_44100_S16_2);
    EXPECT_EQ(packet.timestamp,0x01020304u);
    EXPECT_EQ(packet.mute,0); EXPECT_EQ(packet.gap,0);
    EXPECT_EQ(packet.bytes,(std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(),buffered_block_fixture::plaintext.end())));
  }
  if (!control) EXPECT_EQ(session.realtime_audio_socket,-1);
  EXPECT_EQ(fcntl(socketFd,F_GETFD),-1);
}
INSTANTIATE_TEST_SUITE_P(Routes,RealtimeIngressSockets,
    testing::Values(RealtimeRoute{false,"UdpAudio"},RealtimeRoute{true,"D6Control"}),
    [](const auto &scenario) { return scenario.param.name; });

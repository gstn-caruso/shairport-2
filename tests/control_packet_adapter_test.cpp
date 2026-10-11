#include "session/session_state.hpp"
#include "protocol/rtp/rtp.h"
#include "runtime/common.h"
#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

struct RecordedAnchor { uint64_t clock,time; uint32_t frame,latency; double offset; int warned; };
static std::mutex anchorMutex;
static std::condition_variable anchorChanged;
static std::vector<RecordedAnchor> anchors;
static SessionState *activeSession;
extern "C" void __real__ZN8RtpClock9setAnchorEmjmm(RtpClock *,uint64_t,uint32_t,uint64_t,uint64_t);
extern "C" void __wrap__ZN8RtpClock9setAnchorEmjmm(RtpClock *owner,uint64_t clock,uint32_t frame,uint64_t time,uint64_t now) {
  __real__ZN8RtpClock9setAnchorEmjmm(owner,clock,frame,time,now);
  const auto *session = activeSession;
  { std::lock_guard lock(anchorMutex); anchors.push_back({clock,time,frame,session->latency,config.audio_backend_latency_offset,session->latency_warning_issued}); }
  anchorChanged.notify_all();
}
struct RestoreControlSettings {
  double buffer = config.audio_backend_buffer_desired_length;
  double offset = config.audio_backend_latency_offset;
  double fraction = config.diagnostic_drop_packet_fraction;
  ~RestoreControlSettings() { config.audio_backend_buffer_desired_length = buffer; config.audio_backend_latency_offset = offset; config.diagnostic_drop_packet_fraction = fraction; }
};
static std::vector<uint8_t> controlTiming(uint32_t first,uint32_t second) {
  std::vector<uint8_t> packet(28);
  packet[0] = 0x10; packet[1] = 0xd7;
  const auto bigEndian = [&packet](size_t offset,uint64_t value,unsigned size) {
    for (unsigned index = 0; index < size; ++index) packet[offset+index] = value >> (8*(size-index-1));
  };
  bigEndian(4,first,4); bigEndian(8,0x0102030405060708,8);
  bigEndian(16,second,4); bigEndian(20,0x1122334455667788,8);
  return packet;
}
static void runControlPackets(bool includeInvalid,unsigned rate = 44100,bool negativeZero = false) {
  anchors.clear(); RestoreControlSettings restore;
  config.audio_backend_buffer_desired_length = 0.1;
  config.audio_backend_latency_offset = -10;
  config.diagnostic_drop_packet_fraction = 0;
  SessionState session{}; session.inputAudio.setSetupSampleRate(rate);
  activeSession = &session;
  session.airplay_stream_type = realtime_stream;
  const int descriptor = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(descriptor,0);
  sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(descriptor,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  socklen_t size = sizeof(address);
  ASSERT_EQ(getsockname(descriptor,reinterpret_cast<sockaddr *>(&address),&size),0);
  session.ap2_control_socket = descriptor;
  const int client = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  ASSERT_GE(client,0);
  ASSERT_EQ(connect(client,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
  pthread_t worker;
  ASSERT_EQ(pthread_create(&worker,nullptr,rtp_ap2_control_receiver,&session),0);
  for (uint32_t first : {100000u,200000u}) {
    if (includeInvalid && first == 200000) {
      const auto malformed = controlTiming(0,0x7fffffff);
      EXPECT_EQ(send(client,malformed.data(),malformed.size(),MSG_NOSIGNAL),28);
    }
    const auto packet = controlTiming(first,negativeZero ? first-11026 : first+77175);
    EXPECT_EQ(send(client,packet.data(),packet.size(),MSG_NOSIGNAL),28);
  }
  bool arrived;
  { std::unique_lock lock(anchorMutex); arrived = anchorChanged.wait_for(lock,std::chrono::seconds(1),[]{
      return std::any_of(anchors.begin(),anchors.end(),[](auto anchor){ return anchor.frame == 188975; });
    }); }
  EXPECT_EQ(pthread_cancel(worker),0); EXPECT_EQ(pthread_join(worker,nullptr),0); close(client);
  ASSERT_TRUE(arrived); ASSERT_EQ(anchors.size(),2u);
  EXPECT_EQ(anchors[0].clock,0x1122334455667788u);
  EXPECT_EQ(anchors[0].time,0x0102030405060708u);
  EXPECT_EQ(anchors[0].frame,88975u); EXPECT_EQ(anchors[1].frame,188975u);
  for (const auto &anchor : anchors) {
    EXPECT_EQ(anchor.latency,negativeZero ? UINT32_MAX : rate == 0 ? 88200u : 83790u);
    EXPECT_EQ(anchor.offset,negativeZero ? 0 : rate == 0 ? -10 : 0);
    EXPECT_EQ(anchor.warned,negativeZero || rate != 0 ? 1 : 0);
  }
  EXPECT_EQ(session.ap2_control_socket,-1);
}
TEST(ControlPacketLegacy, SentinelD7PublishesBigEndianAnchorAndZeroesSharedOffsetOnce) { runControlPackets(false); }
TEST(ControlPacketRuntime, InvalidTimingDoesNotPublishAnchorOrLatencyBeforeNextValidPacket) { runControlPackets(true); }
TEST(ControlPacketRuntime, EarlyAnchorBeforeInputRateSetupStillPublishes) { runControlPackets(false,0); }
TEST(ControlPacketRuntime, ZeroRateNegativeLatencyKeepsWarnOnceEffectsWithNonfiniteDiagnosticSeconds) { runControlPackets(false,0,true); }

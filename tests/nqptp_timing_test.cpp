#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <string>

#ifdef NQPTP_REFERENCE
extern "C" {
#include "nqptp-message-handlers.h"
#include "nqptp-ptp-definitions.h"
extern int reset_clock_smoothing;
extern int clock_is_active;
extern uint64_t clock_validity_expiration_time;
void send_awakening_announcement_sequence(uint64_t, const char *, int, uint8_t, uint8_t) {}
}
namespace nqptp {
using ::clocks_private;
using ::shared_memory;
using ::reset_clock_smoothing;
using ::clock_is_active;
using ::clock_validity_expiration_time;
using ::handle_control_port_messages;
using ::handle_announce;
using ::handle_sync;
using ::handle_follow_up;
using ::find_clock_source_record;
using ::ptp_announce_message;
using ::ptp_follow_up_message;
using ::ptp_sync_message;
using ::hcton64;
}
#else
#include "nqptp-message-handlers.h"
#include "nqptp-ptp-definitions.h"
namespace nqptp {
extern int reset_clock_smoothing;
extern int clock_is_active;
extern uint64_t clock_validity_expiration_time;
}
#endif

class NqptpTiming : public ::testing::Test {
protected:
  shm_structure memory{};
  void SetUp() override {
    nqptp::shared_memory = &memory;
    std::memset(nqptp::clocks_private, 0, sizeof(nqptp::clocks_private));
    nqptp::reset_clock_smoothing = 0;
    nqptp::clock_is_active = 0;
    nqptp::clock_validity_expiration_time = 0;
    memory.version = NQPTP_SHM_STRUCTURES_VERSION;
  }
  void TearDown() override { nqptp::shared_memory = nullptr; }
  void control(std::string text, uint64_t time = 1000000000) {
    text = "/nqptp " + text;
    text.push_back('\0');
    nqptp::handle_control_port_messages(text.data(), text.size(), nqptp::clocks_private, time);
  }
  void announce(uint64_t grandmaster = 42) {
    nqptp::ptp_announce_message packet{};
    nqptp::hcton64(42, packet.header.clockIdentity);
    nqptp::hcton64(grandmaster, packet.announce.grandmasterIdentity);
    packet.announce.grandmasterPriority1 = 248;
    packet.announce.grandmasterPriority2 = 248;
    nqptp::handle_announce(reinterpret_cast<char *>(&packet), sizeof(packet),
                           &nqptp::clocks_private[0], 1000000000);
  }
  void follow(uint64_t time, uint64_t origin, int64_t correction = 0) {
    std::array<char, 128> bytes{};
    nqptp::ptp_follow_up_message packet{};
    uint32_t seconds = htonl(origin / 1000000000);
    uint32_t nanoseconds = htonl(origin % 1000000000);
    std::memcpy(packet.follow_up.preciseOriginTimestamp + 2, &seconds, sizeof(seconds));
    std::memcpy(packet.follow_up.preciseOriginTimestamp + 6, &nanoseconds, sizeof(nanoseconds));
    nqptp::hcton64(static_cast<uint64_t>(correction * 65536),
                   reinterpret_cast<uint8_t *>(&packet.header.correctionField));
    std::memcpy(bytes.data(), &packet, sizeof(packet));
    nqptp::handle_follow_up(bytes.data(), bytes.size(), &nqptp::clocks_private[0], time);
  }
  void start() { control("T 127.0.0.2 127.0.0.3"); control("B"); announce(); }
  void expectPublished(uint64_t id, uint64_t time, uint64_t offset, uint64_t start) {
    EXPECT_EQ(memory.main.master_clock_id, id);
    EXPECT_EQ(memory.main.local_time, time);
    EXPECT_EQ(memory.main.local_to_master_time_offset, offset);
    EXPECT_EQ(memory.main.master_clock_start_time, start);
    EXPECT_EQ(std::memcmp(&memory.main, &memory.secondary, sizeof(memory.main)), 0);
  }
};

TEST_F(NqptpTiming, SelectsFirstPeerAndReplacesTheTimingGroup) {
  start();
  follow(1000000000, 10000000000);
  expectPublished(42, 1000000000, 9000000000, 1000000000);
  char first[] = "127.0.0.2";
  char second[] = "127.0.0.3";
  EXPECT_EQ(nqptp::find_clock_source_record(first, nqptp::clocks_private), 0);
  EXPECT_EQ(nqptp::find_clock_source_record(second, nqptp::clocks_private), -1);
  control("T 127.0.0.3");
  EXPECT_EQ(nqptp::find_clock_source_record(first, nqptp::clocks_private), -1);
  EXPECT_EQ(nqptp::find_clock_source_record(second, nqptp::clocks_private), 0);
  announce(43);
  follow(1125000000, 10125000000);
  expectPublished(43, 1125000000, 9000000000, 1125000000);
  control("T");
  expectPublished(0, 0, 0, 0);
  EXPECT_EQ(nqptp::find_clock_source_record(second, nqptp::clocks_private), -1);
}

TEST_F(NqptpTiming, RetainsUpstreamNumericHostAddressSyntax) {
  control("T 127.1");
  EXPECT_STREQ(nqptp::clocks_private[0].ip, "127.1");
  EXPECT_EQ(nqptp::clocks_private[0].family, AF_INET);
  control("T fe80::1%lo");
  EXPECT_STREQ(nqptp::clocks_private[0].ip, "fe80::1%lo");
  EXPECT_EQ(nqptp::clocks_private[0].family, AF_INET6);
}

TEST_F(NqptpTiming, CorrectsFollowUpOffsetAndRetainsUpstreamSyncSemantics) {
  start();
  nqptp::ptp_sync_message sync{};
  nqptp::hcton64(65536 * 1000, reinterpret_cast<uint8_t *>(&sync.header.correctionField));
  nqptp::handle_sync(reinterpret_cast<char *>(&sync), sizeof(sync),
                    &nqptp::clocks_private[0], 1000000000);
  follow(1000000000, 10000000000, 100);
  expectPublished(42, 1000000000, 9000000100, 1000000000);
  nqptp::reset_clock_smoothing = 1;
  follow(1125000000, 10125000000, -100);
  follow(1250000000, 10250000000, -100);
  expectPublished(42, 1250000000, 8999999900, 1250000000);
}

TEST_F(NqptpTiming, SmoothsPositiveAndClampedNegativeJitterAfterWarmup) {
  start();
  follow(1000000000, 10000000000);
  follow(1125000000, 10126000000);
  expectPublished(42, 1125000000, 9001000000, 1000000000);
  follow(1250000000, 10249000000);
  expectPublished(42, 1250000000, 9001000000, 1000000000);
  follow(2250000000, 11270000000);
  expectPublished(42, 2250000000, 9002187500, 1000000000);
  follow(2375000000, 11300000000);
  expectPublished(42, 2375000000, 9002177735, 1000000000);
  announce(43);
  follow(2500000000, 12500000000);
  expectPublished(43, 2500000000, 10000000000, 2500000000);
}

TEST_F(NqptpTiming, PauseContinuesPublishingAndEndAllowsBriefResume) {
  start();
  follow(1000000000, 10000000000);
  control("P", 1100000000);
  follow(1125000000, 10125000000);
  expectPublished(42, 1125000000, 9000000000, 1000000000);
  control("E", 1200000000);
  follow(1250000000, 10250000000);
  expectPublished(0, 0, 0, 0);
  control("B", 1300000000);
  follow(1375000000, 10375000000);
  expectPublished(42, 1375000000, 9000000000, 1000000000);
}

TEST_F(NqptpTiming, ExpiredResumeDiscardsOneSampleBeforeRestartingSmoothing) {
  start();
  follow(1000000000, 10000000000);
  control("E", 1200000000);
  control("B", 4000000000);
  follow(4125000000, 15125000000);
  expectPublished(42, 1000000000, 9000000000, 1000000000);
  follow(4250000000, 15250000000);
  expectPublished(42, 4250000000, 11000000000, 4250000000);
}

TEST_F(NqptpTiming, FollowUpBeforeAnnouncementDoesNotPublish) {
  control("T 127.0.0.2");
  control("B");
  follow(1000000000, 10000000000);
  expectPublished(0, 0, 0, 0);
}

#ifndef NQPTP_REFERENCE
TEST_F(NqptpTiming, RepeatedFirstPeerPreservesMastershipOnBriefResume) {
  start();
  follow(1000000000, 10000000000);
  control("E", 1200000000);
  control("T 127.0.0.2 127.0.0.4", 1300000000);
  control("B", 1400000000);
  announce();
  follow(1500000000, 10500000000);
  expectPublished(42, 1500000000, 9000000000, 1000000000);
}

TEST_F(NqptpTiming, RepeatedActivePeerPreservesSmoothingAndMastership) {
  start();
  follow(1000000000, 10000000000);
  follow(1125000000, 10126000000);
  control("T 127.0.0.2", 7000000000);
  announce();
  follow(7125000000, 16124000000);
  expectPublished(42, 7125000000, 9000992188, 1000000000);
}

TEST_F(NqptpTiming, RepeatedPeerDoesNotExtendBriefResumeValidity) {
  start();
  follow(1000000000, 10000000000);
  control("E", 1200000000);
  control("T 127.0.0.2", 1300000000);
  control("B", 4000000000);
  follow(4125000000, 15125000000);
  expectPublished(42, 1000000000, 9000000000, 1000000000);
  follow(4250000000, 15250000000);
  expectPublished(42, 4250000000, 11000000000, 4250000000);
}

TEST_F(NqptpTiming, RepeatedExpiredPeerStartsFreshMastership) {
  for (uint64_t time : {3450000000ULL, 4000000000ULL}) {
    start();
    follow(1000000000, 10000000000);
    control("E", 1200000000);
    control("T 127.0.0.2", time);
    control("B", time);
    announce();
    follow(time + 125000000, time + 11125000000);
    expectPublished(42, time + 125000000, 11000000000, time + 125000000);
    control("T", time + 250000000);
    expectPublished(0, 0, 0, 0);
  }
}

TEST_F(NqptpTiming, MalformedControlPreservesAnEstablishedTimingGroup) {
  start();
  follow(1000000000, 10000000000);
  for (std::string command : {"/nqptp X", "/other T 127.0.0.3", "/nqptp B extra",
                             "/nqptp T invalid", "/nqptp T 127.0.0.3 invalid",
                             "/nqptp T ", "/nqptp"}) {
    command.push_back('\0');
    nqptp::handle_control_port_messages(command.data(), command.size(),
                                        nqptp::clocks_private, 1100000000);
    EXPECT_STREQ(nqptp::clocks_private[0].ip, "127.0.0.2");
    EXPECT_EQ(nqptp::clocks_private[0].clock_id, 42);
  }
  std::string unterminated = "/nqptp T 127.0.0.3";
  nqptp::handle_control_port_messages(unterminated.data(), unterminated.size(),
                                      nqptp::clocks_private, 1100000000);
  EXPECT_STREQ(nqptp::clocks_private[0].ip, "127.0.0.2");
  expectPublished(42, 1000000000, 9000000000, 1000000000);
}

TEST_F(NqptpTiming, TruncatedPacketsDoNotMutateClockState) {
  start();
  nqptp::clocks_private[0].announcements_without_followups = 3;
  std::array<char, sizeof(nqptp::ptp_follow_up_message)> bytes{};
  for (ssize_t length = 0; length < static_cast<ssize_t>(bytes.size()); ++length) {
    nqptp::handle_follow_up(bytes.data(), length, &nqptp::clocks_private[0], 1000000000);
    EXPECT_EQ(nqptp::clocks_private[0].announcements_without_followups, 3);
  }
  nqptp::handle_announce(bytes.data(), -1, &nqptp::clocks_private[0], 1000000000);
  EXPECT_EQ(nqptp::clocks_private[0].clock_id, 42);
  expectPublished(0, 0, 0, 0);
}

TEST_F(NqptpTiming, MinimalFollowUpWithoutOptionalTlvPublishesSafely) {
  start();
  nqptp::ptp_follow_up_message packet{};
  uint32_t seconds = htonl(10);
  std::memcpy(packet.follow_up.preciseOriginTimestamp + 2, &seconds, sizeof(seconds));
  nqptp::handle_follow_up(reinterpret_cast<char *>(&packet), sizeof(packet),
                          &nqptp::clocks_private[0], 1000000000);
  expectPublished(42, 1000000000, 9000000000, 1000000000);
}
#endif

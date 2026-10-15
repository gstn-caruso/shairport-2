#include "protocol/rtsp/rtsp_message.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <vector>
import receiver.protocol.rtsp.request;

class RequestInput : public RtspRequestInput {
public:
  bool stopped() override {
    if (beforeStop)
      beforeStop();
    return stop || sizes.size() >= stopAfterReads;
  }
  RtspRequestRead read(std::span<char> destination) override {
    sizes.push_back(destination.size());
    if (beforeRead)
      beforeRead();
    if (allocationFails)
      throw std::bad_alloc();
    if (index < fragments.size()) {
      const auto &fragment = fragments[index];
      const auto copied = std::min(fragment.size() - offset, destination.size());
      std::copy_n(fragment.data() + offset, copied, destination.data());
      offset += copied;
      if (offset == fragment.size()) { ++index; offset = 0; }
      return {static_cast<std::ptrdiff_t>(copied), 0, false};
    }
    return terminal;
  }
  bool stop = true;
  std::vector<std::size_t> sizes;
  std::vector<std::string> fragments;
  std::size_t index = 0, offset = 0;
  std::size_t stopAfterReads = std::numeric_limits<std::size_t>::max();
  RtspRequestRead terminal{0, 0, false};
  bool allocationFails = false;
  std::function<void()> beforeRead;
  std::function<void()> beforeStop;
};
class RequestClock : public RtspRequestClock {
public:
  std::uint64_t nowNs() override {
    if (index < times.size())
      return times[index++];
    return now;
  }
  std::uint64_t now = 0;
  std::vector<std::uint64_t> times;
  std::size_t index = 0;
};
class RequestEffects : public RtspRequestEffects {
public:
  void diagnostic(RtspRequestDiagnostic event, RtspRequestPhase phase, int error) override {
    diagnostics.push_back({event, phase, error});
  }
  void closeHeaderChannel() override { ++closes; }
  void stalled() override { ++warnings; }
  struct Diagnostic { RtspRequestDiagnostic event; RtspRequestPhase phase; int error; };
  std::vector<Diagnostic> diagnostics;
  unsigned closes = 0, warnings = 0;
};

TEST(RtspRequestReader, EmptyStoppedInputReturnsNoRequestWithoutReading) {
  RequestInput input;
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, RtspRequestStatus::shutdown);
  EXPECT_FALSE(result.message);
  EXPECT_TRUE(input.sizes.empty());
  EXPECT_EQ(effects.closes, 0u);
}

TEST(RtspRequestReader, FragmentedHeadersAndBodyReadOnlyMissingBytes) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"POST /feed", "back RTSP/1.0\r\nContent-Length: 4\r\n\r\nA", "B", std::string("\0D", 2)};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  ASSERT_TRUE(result.message);
  EXPECT_TRUE(result.message->requestsPath("/feedback"));
  EXPECT_EQ(result.message->bodyText(), std::string_view("AB\0D", 4));
  EXPECT_EQ(input.sizes, (std::vector<std::size_t>{4096, 4086, 3, 2}));
  EXPECT_EQ(effects.closes, 0u);
  EXPECT_EQ(effects.warnings, 0u);
}

struct ReadFailure {
  std::string name;
  RtspRequestPhase phase;
  RtspRequestRead read;
  RtspRequestStatus status;
  RtspRequestDiagnostic diagnostic;
  unsigned closes;
};
void PrintTo(const ReadFailure &scenario, std::ostream *output) { *output << scenario.name; }
class RequestFailures : public testing::TestWithParam<ReadFailure> {};
TEST_P(RequestFailures, ClassifiesFailureAndRetainsNoRequest) {
  const auto &scenario = GetParam();
  RequestInput input;
  input.stop = false;
  input.terminal = scenario.read;
  if (scenario.phase == RtspRequestPhase::body)
    input.fragments = {"POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\nA"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, scenario.status);
  EXPECT_FALSE(result.message);
  EXPECT_EQ(effects.closes, scenario.closes);
  ASSERT_EQ(effects.diagnostics.size(), 1u);
  EXPECT_EQ(effects.diagnostics[0].event, scenario.diagnostic);
  EXPECT_EQ(effects.diagnostics[0].phase, scenario.phase);
  EXPECT_EQ(effects.diagnostics[0].error, scenario.read.errorCode);
}
INSTANTIATE_TEST_SUITE_P(Transport, RequestFailures, testing::Values(
    ReadFailure{"HeaderEof", RtspRequestPhase::headers, {0, 0, false}, RtspRequestStatus::channelClosed, RtspRequestDiagnostic::closed, 1},
    ReadFailure{"HeaderEofWithError", RtspRequestPhase::headers, {0, EAGAIN, false}, RtspRequestStatus::channelClosed, RtspRequestDiagnostic::closed, 1},
    ReadFailure{"HeaderReadError", RtspRequestPhase::headers, {-1, EAGAIN, false}, RtspRequestStatus::readError, RtspRequestDiagnostic::readError, 0},
    ReadFailure{"HeaderTimeout", RtspRequestPhase::headers, {-1, ETIMEDOUT, true}, RtspRequestStatus::shutdown, RtspRequestDiagnostic::timeout, 0},
    ReadFailure{"HeaderStaleTimeoutAtEof", RtspRequestPhase::headers, {0, ETIMEDOUT, true}, RtspRequestStatus::shutdown, RtspRequestDiagnostic::timeout, 0},
    ReadFailure{"BodyEof", RtspRequestPhase::body, {0, 0, false}, RtspRequestStatus::channelClosed, RtspRequestDiagnostic::closed, 0},
    ReadFailure{"BodyEofWithError", RtspRequestPhase::body, {0, EAGAIN, false}, RtspRequestStatus::channelClosed, RtspRequestDiagnostic::closed, 0},
    ReadFailure{"BodyReadError", RtspRequestPhase::body, {-1, EAGAIN, false}, RtspRequestStatus::readError, RtspRequestDiagnostic::readError, 0},
    ReadFailure{"BodyTimeout", RtspRequestPhase::body, {-1, ETIMEDOUT, true}, RtspRequestStatus::shutdown, RtspRequestDiagnostic::timeout, 0},
    ReadFailure{"BodyStaleTimeoutAtEof", RtspRequestPhase::body, {0, ETIMEDOUT, true}, RtspRequestStatus::shutdown, RtspRequestDiagnostic::timeout, 0}
), [](const auto &scenario) { return scenario.param.name; });

TEST(RtspRequestReader, BadHeaderReleasesRequestWithoutClosingChannel) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"OPTIONS /info RTSP/1.0\r\nBroken:header\r\n"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, RtspRequestStatus::badPacket);
  EXPECT_FALSE(result.message);
  EXPECT_EQ(effects.closes, 0u);
}

TEST(RtspRequestReader, AllocationFailureReturnsTypedStatusWithoutClosingChannel) {
  RequestInput input;
  input.stop = false;
  input.allocationFails = true;
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, RtspRequestStatus::allocationFailure);
  EXPECT_FALSE(result.message);
  EXPECT_EQ(effects.closes, 0u);
  ASSERT_EQ(effects.diagnostics.size(), 1u);
  EXPECT_EQ(effects.diagnostics[0].event, RtspRequestDiagnostic::allocationFailure);
}

TEST(RtspRequestReader, StallWarningUsesStrictThresholdAndOccursOnceBeforeReading) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\n", "A", "B", "C"};
  RequestClock clock;
  clock.times = {0, 15000000000, 15000000001};
  RequestEffects effects;
  std::vector<unsigned> warningsAtRead;
  input.beforeRead = [&] { warningsAtRead.push_back(effects.warnings); };
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  EXPECT_EQ(result.message->bodyText(), "ABC");
  EXPECT_EQ(effects.warnings, 1u);
  EXPECT_EQ(warningsAtRead, (std::vector<unsigned>{0, 0, 1, 1}));
}

TEST(RtspRequestReader, BodyStallWarningPrecedesStopAndNoBodyReadOccurs) {
  RequestInput input;
  input.stop = false;
  input.stopAfterReads = 1;
  input.fragments = {"POST /feedback RTSP/1.0\r\nContent-Length: 3\r\n\r\n"};
  RequestClock clock;
  clock.times = {0, 15000000001};
  RequestEffects effects;
  std::vector<unsigned> warningsAtStop;
  input.beforeStop = [&] { warningsAtStop.push_back(effects.warnings); };
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, RtspRequestStatus::shutdown);
  EXPECT_FALSE(result.message);
  EXPECT_EQ(input.sizes.size(), 1u);
  EXPECT_EQ(effects.warnings, 1u);
  EXPECT_EQ(warningsAtStop, (std::vector<unsigned>{0, 1}));
  ASSERT_EQ(effects.diagnostics.size(), 1u);
  EXPECT_EQ(effects.diagnostics[0].phase, RtspRequestPhase::body);
}

TEST(RtspRequestReader, HeaderStopAfterPartialLineRetainsNoRequest) {
  RequestInput input;
  input.stop = false;
  input.stopAfterReads = 1;
  input.fragments = {"OPTIONS /info"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  EXPECT_EQ(result.status, RtspRequestStatus::shutdown);
  EXPECT_FALSE(result.message);
  EXPECT_EQ(input.sizes.size(), 1u);
  EXPECT_EQ(effects.closes, 0u);
}

TEST(RtspRequestReader, TerminalCarriageReturnIsConsumedWithinEachFragment) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"OPTIONS /info RTSP/1.0\r", "Content-Length: 0\r", "\r"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  EXPECT_TRUE(result.message->requestsPath("/info"));
  EXPECT_TRUE(result.message->bodyText().empty());
}

TEST(RtspRequestReader, SplitCrLfPreservesFollowingHeaders) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"OPTIONS /info RTSP/1.0\r", "\nCSeq: 1\r\n\r\n"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  EXPECT_STREQ(result.message->headerValue("CSeq"), "1");
  EXPECT_TRUE(result.message->bodyText().empty());
}

TEST(RtspRequestReader, SplitHeaderTerminatorPreservesBinaryBodyAndFollowingRequests) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"POST /feedback RTSP/1.0\r", "\nContent-Length: 3\r\n\r",
                     std::string("\nA\0", 3),
                     "BOPTIONS /next RTSP/1.0\r\n\r\nPOST /last RTSP/1.0\r\nContent-Length: 2\r\n\r\nCD"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto first = reader.read();
  ASSERT_EQ(first.status, RtspRequestStatus::success);
  EXPECT_EQ(first.message->bodyText(), std::string_view("A\0B", 3));
  auto second = reader.read();
  ASSERT_EQ(second.status, RtspRequestStatus::success);
  EXPECT_TRUE(second.message->requestsMethod("OPTIONS"));
  EXPECT_TRUE(second.message->requestsPath("/next"));
  EXPECT_TRUE(second.message->bodyText().empty());
  auto third = reader.read();
  ASSERT_EQ(third.status, RtspRequestStatus::success);
  EXPECT_TRUE(third.message->requestsMethod("POST"));
  EXPECT_EQ(third.message->bodyText(), "CD");
  EXPECT_EQ(reader.read().status, RtspRequestStatus::channelClosed);
}

TEST(RtspRequestReader, SplitEmptyBodyTerminatorDoesNotBecomeNextRequest) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"OPTIONS /first RTSP/1.0\r\n\r", "\nGET /second RTSP/1.0\r\n\r\n"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto first = reader.read();
  ASSERT_EQ(first.status, RtspRequestStatus::success);
  EXPECT_TRUE(first.message->requestsPath("/first"));
  auto second = reader.read();
  ASSERT_EQ(second.status, RtspRequestStatus::success);
  EXPECT_TRUE(second.message->requestsMethod("GET"));
  EXPECT_TRUE(second.message->requestsPath("/second"));
}

TEST(RtspRequestReader, BufferedRequestsBelongToTheirReader) {
  RequestInput firstInput, secondInput;
  firstInput.stop = secondInput.stop = false;
  firstInput.fragments = {"OPTIONS /first RTSP/1.0\r\n\r\nGET /buffered RTSP/1.0\r\n\r\n"};
  secondInput.fragments = {"POST /independent RTSP/1.0\r\nContent-Length: 1\r\n\r\nB"};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader firstReader(firstInput, clock, effects);
  RtspRequestReader secondReader(secondInput, clock, effects);
  ASSERT_EQ(firstReader.read().status, RtspRequestStatus::success);
  auto independent = secondReader.read();
  ASSERT_EQ(independent.status, RtspRequestStatus::success);
  EXPECT_TRUE(independent.message->requestsPath("/independent"));
  EXPECT_EQ(independent.message->bodyText(), "B");
  auto buffered = firstReader.read();
  ASSERT_EQ(buffered.status, RtspRequestStatus::success);
  EXPECT_TRUE(buffered.message->requestsMethod("GET"));
  EXPECT_TRUE(buffered.message->requestsPath("/buffered"));
}

TEST(RtspRequestReader, BodyLargerThanHeaderBufferReadsDeclaredMissingLength) {
  RequestInput input;
  input.stop = false;
  input.fragments = {"POST /feedback RTSP/1.0\r\nContent-Length: 5000\r\n\r\n", std::string(5000, 'X')};
  RequestClock clock;
  RequestEffects effects;
  RtspRequestReader reader(input, clock, effects);
  auto result = reader.read();
  ASSERT_EQ(result.status, RtspRequestStatus::success);
  EXPECT_EQ(result.message->bodyText(), std::string(5000, 'X'));
  EXPECT_EQ(input.sizes, (std::vector<std::size_t>{4096, 5000}));
}

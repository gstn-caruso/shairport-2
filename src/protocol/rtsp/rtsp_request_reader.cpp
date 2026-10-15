module;
#include "protocol/rtsp/rtsp_message.hpp"
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
module receiver.protocol.rtsp.request;

struct RtspRequestReader::Pending {
  std::unique_ptr<RtspMessage> message = std::make_unique<RtspMessage>();
  std::unique_ptr<char[]> bytes = std::make_unique_for_overwrite<char[]>(4096);
  std::size_t capacity = 4096;
  std::size_t used = 0;
  int contentLength = -1;
};

bool RtspRequestReader::stopped(RtspRequestPhase phase) {
  if (!input_.stopped())
    return false;
  effects_.diagnostic(RtspRequestDiagnostic::shutdown, phase, 0);
  return true;
}

void RtspRequestReader::consumeLineFeed(Pending &request) {
  if (!buffer_.skipLineFeed || request.used == 0)
    return;
  if (request.bytes[0] == '\n') {
    --request.used;
    std::memmove(request.bytes.get(), request.bytes.get() + 1, request.used);
  }
  buffer_.skipLineFeed = false;
}

std::optional<RtspRequestStatus> RtspRequestReader::receive(Pending &request, std::size_t count,
                                                          RtspRequestPhase phase) {
  const auto received = input_.read(std::span(request.bytes.get(), request.capacity).subspan(request.used, count));
  if (received.count > 0) {
    request.used += received.count;
    consumeLineFeed(request);
    return std::nullopt;
  }
  if (received.timedOut) {
    effects_.diagnostic(RtspRequestDiagnostic::timeout, phase, received.errorCode);
    return RtspRequestStatus::shutdown;
  }
  if (received.count == 0) {
    effects_.diagnostic(RtspRequestDiagnostic::closed, phase, received.errorCode);
    if (phase == RtspRequestPhase::headers)
      effects_.closeHeaderChannel();
    return RtspRequestStatus::channelClosed;
  }
  effects_.diagnostic(RtspRequestDiagnostic::readError, phase, received.errorCode);
  return RtspRequestStatus::readError;
}

std::optional<RtspRequestStatus> RtspRequestReader::readHeaders(Pending &request) {
  bool hasBufferedInput = request.used > 0;
  while (request.contentLength < 0) {
    if (stopped(RtspRequestPhase::headers))
      return RtspRequestStatus::shutdown;
    if (!hasBufferedInput) {
      if (auto failure = receive(request, request.capacity - request.used, RtspRequestPhase::headers))
        return failure;
    }
    hasBufferedInput = false;
    while (request.contentLength < 0) {
      consumeLineFeed(request);
      const std::string_view buffered(request.bytes.get(), request.used);
      const auto delimiter = buffered.find_first_of("\r\n");
      if (delimiter == std::string_view::npos)
        break;
      auto consumed = delimiter + 1;
      if (buffered[delimiter] == '\r' && consumed < buffered.size() && buffered[consumed] == '\n')
        ++consumed;
      buffer_.skipLineFeed = buffered[delimiter] == '\r' && consumed == delimiter + 1;
      auto line = buffered.substr(0, delimiter);
      line = line.substr(0, line.find('\0'));
      const auto parsed = request.message->readLine(line);
      if (!parsed) {
        effects_.diagnostic(RtspRequestDiagnostic::badPacket, RtspRequestPhase::headers, 0);
        return RtspRequestStatus::badPacket;
      }
      request.contentLength = *parsed;
      request.used -= consumed;
      std::memmove(request.bytes.get(), request.bytes.get() + consumed, request.used);
    }
  }
  return std::nullopt;
}

std::optional<RtspRequestStatus> RtspRequestReader::readBody(Pending &request) {
  consumeLineFeed(request);
  if (request.contentLength <= 0)
    return std::nullopt;
  const auto threshold = clock_.nowNs() + std::uint64_t{15000000000};
  const auto expected = static_cast<std::size_t>(request.contentLength);
  if (expected > request.capacity) {
    auto grown = std::make_unique_for_overwrite<char[]>(expected);
    std::memcpy(grown.get(), request.bytes.get(), request.used);
    request.bytes = std::move(grown);
    request.capacity = expected;
  }
  bool warned = false;
  while (request.used < expected) {
    if (!warned && clock_.nowNs() > threshold) {
      effects_.stalled();
      warned = true;
    }
    if (stopped(RtspRequestPhase::body))
      return RtspRequestStatus::shutdown;
    if (auto failure = receive(request, expected - request.used, RtspRequestPhase::body))
      return failure;
  }
  return std::nullopt;
}

RtspRequestResult RtspRequestReader::read() {
  auto phase = RtspRequestPhase::headers;
  try {
    Pending request;
    request.used = buffer_.bytes.size();
    std::memcpy(request.bytes.get(), buffer_.bytes.data(), request.used);
    buffer_.bytes.clear();
    if (auto failure = readHeaders(request))
      return {*failure, {}};
    phase = RtspRequestPhase::body;
    if (auto failure = readBody(request))
      return {*failure, {}};
    const auto length = static_cast<std::size_t>(request.contentLength);
    request.message->replaceBody(std::string_view(request.bytes.get(), length));
    buffer_.bytes.assign(request.bytes.get() + length, request.used - length);
    return {RtspRequestStatus::success, std::move(request.message)};
  } catch (const std::bad_alloc &) {
    effects_.diagnostic(RtspRequestDiagnostic::allocationFailure, phase, 0);
    return {RtspRequestStatus::allocationFailure, {}};
  }
}

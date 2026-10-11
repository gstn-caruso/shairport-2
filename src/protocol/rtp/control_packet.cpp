module;
#include <bit>
#include <cstdint>
#include <expected>
#include <optional>
#include <cmath>
#include <limits>
#include <span>
#include <functional>
module receiver.protocol.ap2.control_packet;

namespace {
std::optional<std::int32_t> truncatedFrames(double seconds,unsigned rate) {
  const auto frames = seconds*rate;
  if (!std::isfinite(frames)) return std::nullopt;
  const auto truncated = std::trunc(frames);
  if (truncated < std::numeric_limits<std::int32_t>::min() || truncated > std::numeric_limits<std::int32_t>::max())
    return std::nullopt;
  return static_cast<std::int32_t>(truncated);
}
}
ControlDecision ControlPacketCoordinator::handle(ControlRead read,ControlLiveSettings,
                                                const std::function<double()> &) {
  ControlDecision decision;
  if (!initialized_) {
    initialized_ = true; packetCount_ = 0;
    decision.append({ControlEffectKind::initialized});
  }
  if (read.errorCode != 0) {
    ControlEffect error{ControlEffectKind::error}; error.errorCode = read.errorCode;
    decision.append(error); return decision;
  }
  if (read.bytes.size() < 28) decision.append({ControlEffectKind::malformed,read.bytes});
  return decision;
}
namespace {
bool fitsLatency(std::int64_t value) {
  return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}
}

std::expected<ControlLatencyResult,ControlLatencyError> ControlLatency::calculate(
    std::uint32_t frame1,std::uint32_t frame2,unsigned inputRate,double backendBufferSeconds,
    double sharedOffsetSeconds,bool warningIssued) {
  const auto stream = std::bit_cast<std::int32_t>(frame2-frame1);
  const auto buffer = truncatedFrames(backendBufferSeconds,inputRate);
  const auto offset = truncatedFrames(sharedOffsetSeconds,inputRate);
  if (inputRate == 0 || !buffer || !offset) return std::unexpected(ControlLatencyError::invalidLatency);
  const auto source = std::int64_t{stream} + 11025 - *buffer;
  auto total = source + *offset;
  if (!fitsLatency(source) || !fitsLatency(total)) return std::unexpected(ControlLatencyError::invalidLatency);
  const bool warn = total <= 0 && !warningIssued;
  if (warn) total = source;
  return ControlLatencyResult{stream,static_cast<std::int32_t>(source),static_cast<std::int32_t>(total),warn};
}

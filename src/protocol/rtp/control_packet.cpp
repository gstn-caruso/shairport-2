module;
#include <bit>
#include <cstdint>
#include <expected>
#include <optional>
#include <cmath>
#include <limits>
#include <span>
#include <functional>
#include <cerrno>
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
ControlDecision ControlPacketCoordinator::handle(ControlRead read,ControlLiveSettings settings,
                                                const std::function<double()> &draw) {
  ControlDecision decision;
  if (!initialized_) {
    initialized_ = true; packetCount_ = 0;
    decision.append({ControlEffectKind::initialized});
  }
  if (read.errorCode != 0) {
    if (read.errorCode == EAGAIN || read.errorCode == EWOULDBLOCK) {
      if (settings.realtime) {
        packetCount_ = 0;
        decision.append({ControlEffectKind::resetAnchor});
      }
      return decision;
    }
    ControlEffect error{ControlEffectKind::error}; error.errorCode = read.errorCode;
    decision.append(error); return decision;
  }
  if (read.bytes.size() < 28) {
    ControlEffect malformed{ControlEffectKind::malformed}; malformed.length = read.bytes.size();
    decision.append(malformed); return decision;
  }
  const auto elapsed = std::bit_cast<std::int64_t>(read.nowNs-startNs_);
  const bool sentinel = (read.bytes[0] & 0x10) != 0;
  if (elapsed < 2000000 && !sentinel) {
    ControlEffect stale{ControlEffectKind::stale}; stale.elapsedNs = elapsed;
    decision.append(stale); return decision;
  }
  ++packetCount_;
  if (packetCount_ == 1) {
    ControlEffect first{ControlEffectKind::firstSentinel}; first.sentinel = sentinel;
    decision.append(first);
  }
  if (packetCount_ == 0) return decision;
  if (settings.dropFraction != 0 && !(draw() > settings.dropFraction)) {
    decision.append({ControlEffectKind::diagnosticDrop}); return decision;
  }
  decision.append({ControlEffectKind::capturePeerIfAbsent});
  if (read.bytes[1] == 0xd7) {
    const auto bigEndian = [&read](std::size_t offset,unsigned count) {
      std::uint64_t value = 0;
      for (unsigned index = 0; index < count; ++index) value = (value << 8) | read.bytes[offset+index];
      return value;
    };
    const auto frame1 = static_cast<std::uint32_t>(bigEndian(4,4));
    const auto frame2 = static_cast<std::uint32_t>(bigEndian(16,4));
    const auto stream = std::bit_cast<std::int32_t>(frame2-frame1);
    if (stream != 77175) {
      ControlEffect unexpected{ControlEffectKind::unexpectedLatency}; unexpected.stream = stream;
      decision.append(unexpected);
    }
    const auto latency = ControlLatency::calculate(frame1,frame2,settings.inputRate,
        settings.backendBufferSeconds,settings.sharedOffsetSeconds,settings.warningIssued);
    if (!latency) {
      decision.append({ControlEffectKind::invalidLatency}); return decision;
    }
    if (latency->warnAndZero) {
      ControlEffect warning{ControlEffectKind::warnAndZeroSharedOffset}; warning.stream = latency->stream;
      decision.append(warning);
    }
    ControlEffect setLatency{ControlEffectKind::setLatency}; setLatency.latency = latency->total;
    decision.append(setLatency);
    ControlEffect anchor{ControlEffectKind::setAnchor};
    anchor.clockId = bigEndian(20,8); anchor.timeNs = bigEndian(8,8); anchor.anchorFrame = frame1-11025;
    decision.append(anchor);
  } else if (read.bytes[1] == 0xd6) {
    decision.append({ControlEffectKind::retransmit,read.bytes.subspan(6)});
  } else {
    decision.append({ControlEffectKind::unknown,read.bytes});
  }
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
  if (!buffer || !offset) return std::unexpected(ControlLatencyError::invalidLatency);
  const auto source = std::int64_t{stream} + 11025 - *buffer;
  auto total = source + *offset;
  if (!fitsLatency(source) || !fitsLatency(total)) return std::unexpected(ControlLatencyError::invalidLatency);
  const bool warn = total <= 0 && !warningIssued;
  if (warn) total = source;
  return ControlLatencyResult{stream,static_cast<std::int32_t>(source),static_cast<std::int32_t>(total),warn};
}

module;
#include <bit>
#include <cstdint>
#include <expected>
module receiver.protocol.ap2.control_packet;

std::expected<ControlLatencyResult,ControlLatencyError> ControlLatency::calculate(
    std::uint32_t frame1,std::uint32_t frame2,unsigned inputRate,double backendBufferSeconds,
    double sharedOffsetSeconds,bool warningIssued) {
  const auto stream = std::bit_cast<std::int32_t>(frame2-frame1);
  const std::int32_t source = stream + 11025 - static_cast<std::int32_t>(backendBufferSeconds*inputRate);
  std::int32_t total = source + static_cast<std::int32_t>(sharedOffsetSeconds*inputRate);
  const bool warn = total <= 0 && !warningIssued;
  if (warn) total = source;
  return ControlLatencyResult{stream,source,total,warn};
}

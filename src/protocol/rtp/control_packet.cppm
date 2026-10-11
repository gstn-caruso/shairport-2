module;
#include <cstdint>
#include <expected>
export module receiver.protocol.ap2.control_packet;

export enum class ControlLatencyError { invalidLatency };
export struct ControlLatencyResult {
  std::int32_t stream,source,total;
  bool warnAndZero;
};
export class ControlLatency {
public:
  static std::expected<ControlLatencyResult,ControlLatencyError> calculate(
      std::uint32_t frame1,std::uint32_t frame2,unsigned inputRate,double backendBufferSeconds,
      double sharedOffsetSeconds,bool warningIssued);
};

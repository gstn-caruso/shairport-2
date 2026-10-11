module;
#include <cstdint>
#include <expected>
#include <span>
#include <array>
#include <functional>
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
export struct ControlRead { std::span<const std::uint8_t> bytes; int errorCode; std::uint64_t nowNs; };
export struct ControlLiveSettings {
  bool realtime;
  unsigned inputRate;
  double backendBufferSeconds,sharedOffsetSeconds;
  bool warningIssued;
  double dropFraction;
};
export enum class ControlEffectKind {
  initialized,stale,firstSentinel,diagnosticDrop,capturePeerIfAbsent,unexpectedLatency,
  warnAndZeroSharedOffset,setLatency,setAnchor,retransmit,unknown,resetAnchor,error,malformed,invalidLatency
};
export struct ControlEffect {
  ControlEffectKind kind = ControlEffectKind::initialized;
  // Retransmit/unknown views are borrowed until the caller overwrites its read storage.
  std::span<const std::uint8_t> bytes{};
  std::size_t length = 0;
  std::int64_t elapsedNs = 0;
  bool sentinel = false;
  std::int32_t stream = 0,latency = 0;
  std::uint32_t anchorFrame = 0;
  std::uint64_t clockId = 0,timeNs = 0;
  int errorCode = 0;
};
export class ControlPacketCoordinator;
export class ControlDecision {
public:
  std::size_t size() const { return count_; }
  const ControlEffect &operator[](std::size_t index) const { return effects_[index]; }
  const ControlEffect *begin() const { return effects_.data(); }
  const ControlEffect *end() const { return effects_.data()+count_; }
private:
  friend class ControlPacketCoordinator;
  void append(ControlEffect effect) { effects_[count_++] = effect; }
  std::array<ControlEffect,8> effects_{};
  std::size_t count_ = 0;
};
export class ControlPacketCoordinator {
public:
  explicit ControlPacketCoordinator(std::uint64_t startNs) : startNs_(startNs) {}
  ControlDecision handle(ControlRead,ControlLiveSettings,const std::function<double()> &draw);
private:
  const std::uint64_t startNs_;
  bool initialized_ = false;
  std::uint64_t packetCount_ = 0;
};

#include "protocol/rtp/resend_coordinator.hpp"
#include <array>

ResendOutcome ResendCoordinator::request(bool active,bool hasPeer,uint16_t first,
                                         uint32_t count,double dropFraction,ResendIo &io) {
  std::lock_guard lock(mutex_);
  if (!active) return {ResendStatus::inactive};
  if (!hasPeer) return {ResendStatus::noPeer};
  io.nowNs();
  if (dropFraction != 0 && !(io.draw() > dropFraction)) return {ResendStatus::dropped};
  const std::array<uint8_t,8> packet{0x80,0xd5,0,1,static_cast<uint8_t>(first>>8),
      static_cast<uint8_t>(first),static_cast<uint8_t>(count>>8),static_cast<uint8_t>(count)};
  const auto sent = io.send(packet);
  return {sent.returnedBytes == -1 ? ResendStatus::sendFailed : ResendStatus::sent,
          sent.errorCode,sent.timeoutSetupError};
}

void ResendCoordinator::reset() {
  std::lock_guard lock(mutex_);
  lastFailureNs_ = 0;
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

struct ResendSendResult {
  std::ptrdiff_t returnedBytes;
  int errorCode;
  int timeoutSetupError;
};

class ResendIo {
public:
  virtual ~ResendIo() = default;
  virtual uint64_t nowNs() = 0;
  virtual double draw() = 0;
  virtual ResendSendResult send(std::span<const uint8_t,8> packet) = 0;
};

enum class ResendStatus { inactive,noPeer,suppressed,dropped,sent,sendFailed };
struct ResendOutcome {
  ResendStatus status;
  int errorCode = 0;
  int timeoutSetupError = 0;
};

class ResendCoordinator {
public:
  // IO runs synchronously inside the transaction; it must not reenter this coordinator.
  ResendOutcome request(bool active,bool hasPeer,uint16_t first,uint32_t count,
                        double dropFraction,ResendIo &io);
  void reset();

private:
  std::mutex mutex_;
  uint64_t lastFailureNs_ = 0;
};

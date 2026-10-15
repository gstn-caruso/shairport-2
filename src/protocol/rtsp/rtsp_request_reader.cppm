module;
#include "protocol/rtsp/rtsp_message.hpp"
#include "protocol/rtsp/rtsp_request_buffer.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

export module receiver.protocol.rtsp.request;

export struct RtspRequestRead {
  std::ptrdiff_t count;
  int errorCode;
  bool timedOut;
};
export class RtspRequestInput {
public:
  virtual ~RtspRequestInput() = default;
  virtual bool stopped() = 0;
  // Read at most destination.size() bytes. Capture the transport error even at EOF;
  // timedOut preserves transport timeout classification before EOF classification.
  virtual RtspRequestRead read(std::span<char> destination) = 0;
};
export class RtspRequestClock {
public:
  virtual ~RtspRequestClock() = default;
  virtual std::uint64_t nowNs() = 0;
};
export enum class RtspRequestPhase { headers, body };
export enum class RtspRequestDiagnostic { shutdown, timeout, closed, readError, badPacket, allocationFailure };
export class RtspRequestEffects {
public:
  virtual ~RtspRequestEffects() = default;
  virtual void diagnostic(RtspRequestDiagnostic event, RtspRequestPhase phase, int errorCode) = 0;
  virtual void closeHeaderChannel() = 0;
  virtual void stalled() = 0;
};
export enum class RtspRequestStatus { success, shutdown, badPacket, channelClosed, readError, allocationFailure };
export struct RtspRequestResult {
  RtspRequestStatus status;
  std::unique_ptr<RtspMessage> message;
};
export class RtspRequestReader {
public:
  RtspRequestReader(RtspRequestInput &input, RtspRequestClock &clock, RtspRequestEffects &effects,
                    RtspRequestBuffer *buffer = nullptr)
      : input_(input), clock_(clock), effects_(effects), buffer_(buffer ? *buffer : ownedBuffer_) {}
  // Allocation failures become a result without a message. Other exceptions propagate;
  // in particular, cancellation unwinding releases pending storage through RAII.
  RtspRequestResult read();
private:
  struct Pending;
  std::optional<RtspRequestStatus> readHeaders(Pending &request);
  std::optional<RtspRequestStatus> readBody(Pending &request);
  std::optional<RtspRequestStatus> receive(Pending &request, std::size_t count, RtspRequestPhase phase);
  bool stopped(RtspRequestPhase phase);
  RtspRequestInput &input_;
  RtspRequestClock &clock_;
  RtspRequestEffects &effects_;
  RtspRequestBuffer ownedBuffer_;
  RtspRequestBuffer &buffer_;
};

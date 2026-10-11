#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

class EventPlaintextChannel {
public:
  virtual ~EventPlaintextChannel() = default;
  virtual std::ptrdiff_t write(std::span<const uint8_t> bytes) = 0;
  virtual std::ptrdiff_t read(std::span<uint8_t> bytes) = 0;
};
enum class EventDiagnosticKind { notOpen,written,read,reply,closed };
struct EventDiagnostic {
  EventDiagnosticKind kind;
  std::ptrdiff_t count = 0;
  std::span<const uint8_t> bytes;
};
class EventObserver {
public:
  virtual ~EventObserver() = default;
  // Callbacks run inside the exchange transaction: no reentry or retained borrowed views.
  virtual void observe(const EventDiagnostic &event) = 0;
};
enum class EventStatus { notOpen,writeFailed,readFailed,closed,replied };
struct EventResult {
  EventStatus status;
  std::ptrdiff_t count;
  std::vector<uint8_t> reply;
};
class EventCommandExchange {
public:
  bool attach(std::unique_ptr<EventPlaintextChannel> channel);
  void close();
  EventResult postBinaryCommand(std::span<const uint8_t> body,EventObserver &observer);
private:
  std::mutex mutex_;
  std::unique_ptr<EventPlaintextChannel> channel_;
};

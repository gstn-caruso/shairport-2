#include "protocol/ap2/event_command_exchange.hpp"
#include <array>
#include <string>

bool EventCommandExchange::attach(std::unique_ptr<EventPlaintextChannel> channel) {
  if (!channel) return false;
  std::lock_guard lock(mutex_);
  if (channel_) return false;
  channel_ = std::move(channel);
  return true;
}
void EventCommandExchange::close() { std::lock_guard lock(mutex_); channel_.reset(); }

EventResult EventCommandExchange::postBinaryCommand(std::span<const uint8_t> body,EventObserver &observer) {
  std::lock_guard lock(mutex_);
  if (!channel_) {
    observer.observe({EventDiagnosticKind::notOpen});
    return {EventStatus::notOpen,-1,{}};
  }
  const std::string header = "POST /command RTSP/1.0\r\nContent-Length: "+std::to_string(body.size())+
      "\r\nContent-Type: application/x-apple-binary-plist\r\n\r\n";
  std::vector<uint8_t> packet(header.begin(),header.end()); packet.insert(packet.end(),body.begin(),body.end());
  const auto written = channel_->write(packet);
  if (written < 0 || static_cast<size_t>(written) != packet.size()) return {EventStatus::writeFailed,-1,{}};
  observer.observe({EventDiagnosticKind::written,written});
  std::array<uint8_t,4096> reply;
  const auto count = channel_->read(reply);
  observer.observe({EventDiagnosticKind::read,count});
  if (count <= 0) {
    observer.observe({EventDiagnosticKind::closed});
    return {count == 0 ? EventStatus::closed : EventStatus::readFailed,count,{}};
  }
  EventResult result{EventStatus::replied,count,{reply.begin(),reply.begin()+count}};
  observer.observe({EventDiagnosticKind::reply,count,result.reply});
  return result;
}

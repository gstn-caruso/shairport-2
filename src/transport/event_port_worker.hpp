#pragma once
#include "protocol/ap2/event_command_exchange.hpp"
#include <expected>
#include <functional>

class EventChannelInterrupt {
public:
  virtual ~EventChannelInterrupt() = default;
  virtual void interrupt() noexcept = 0;
};
struct AcceptedEventChannel {
  std::unique_ptr<EventPlaintextChannel> channel;
  std::shared_ptr<EventChannelInterrupt> interrupt;
};
class EventPortWorker {
public:
  using ChannelFactory = std::function<std::expected<AcceptedEventChannel,int>(int)>;
  EventPortWorker() = default;
  ~EventPortWorker();
  // Start consumes the bound descriptor on every outcome. The exchange outlives the worker.
  std::expected<void,int> start(int boundFd,EventCommandExchange &exchange,
                              ChannelFactory factory,std::function<void()> initialCommand);
  int stop();
};

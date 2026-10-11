#pragma once
#include "protocol/ap2/event_command_exchange.hpp"
#include <expected>
#include <functional>
#include <condition_variable>
#include <thread>

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
private:
  void run();
  std::mutex mutex_;
  std::condition_variable joined_;
  std::jthread thread_;
  EventCommandExchange *exchange_ = nullptr;
  int listener_ = -1,wake_ = -1,error_ = 0;
  bool stopping_ = false,joining_ = false;
};

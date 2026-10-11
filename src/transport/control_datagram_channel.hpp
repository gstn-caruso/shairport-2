#pragma once
#include "transport/remote_control_peer.hpp"
#include <condition_variable>
#include <cstdint>
#include <span>

struct ControlSendResult { std::ptrdiff_t count; int errorCode; bool stopped = false; };
struct ControlReceiveResult {
  std::ptrdiff_t count = -1;
  int errorCode = 0;
  bool stopped = false;
  RemoteControlPeerSnapshot peer;
};

class ControlDatagramChannel {
public:
  class SendLease {
  public:
    SendLease() = default;
    SendLease(const SendLease &) = delete;
    SendLease &operator=(const SendLease &) = delete;
    SendLease(SendLease &&other) noexcept;
    SendLease &operator=(SendLease &&other) noexcept;
    ~SendLease();
    explicit operator bool() const { return owner_ != nullptr; }
    int setResendTimeout();
    ControlSendResult sendTo(std::span<const uint8_t> bytes,const RemoteControlPeerSnapshot &peer);
  private:
    friend class ControlDatagramChannel;
    SendLease(ControlDatagramChannel *owner,int fd,int wake) : owner_(owner),fd_(fd),wake_(wake) {}
    ControlDatagramChannel *owner_ = nullptr;
    int fd_ = -1,wake_ = -1;
  };

  ControlDatagramChannel() = default;
  ~ControlDatagramChannel();
  // Adoption transfers fd only on success. Re-adoption requires the previous receiver joined.
  int adopt(int fd);
  void activate();
  void deactivate();
  SendLease trySendLease();
  ControlReceiveResult receive(std::span<uint8_t> bytes);
  void deactivateAndRequestStop();
  // The owner outlives its leases. A holder must release its lease before calling close.
  void close();
private:
  SendLease acquire(bool requireActive);
  void release();
  bool stopped();
  std::mutex mutex_;
  std::condition_variable released_;
  int fd_ = -1,wake_ = -1;
  bool active_ = false,stopped_ = false,closing_ = false;
  unsigned operations_ = 0;
};

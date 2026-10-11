#include "transport/control_datagram_channel.hpp"
#include <cerrno>
#include <chrono>
#include <utility>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

ControlDatagramChannel::SendLease::SendLease(SendLease &&other) noexcept
    : owner_(std::exchange(other.owner_,nullptr)),fd_(other.fd_),wake_(other.wake_) {}
ControlDatagramChannel::SendLease &ControlDatagramChannel::SendLease::operator=(SendLease &&other) noexcept {
  if (this != &other) {
    if (owner_) owner_->release();
    owner_ = std::exchange(other.owner_,nullptr); fd_ = other.fd_; wake_ = other.wake_;
  }
  return *this;
}
ControlDatagramChannel::SendLease::~SendLease() { if (owner_) owner_->release(); }

int ControlDatagramChannel::adopt(int fd) {
  int type = 0; socklen_t size = sizeof(type);
  if (getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size) < 0) return errno;
  if (type != SOCK_DGRAM) return EINVAL;
  const int wake = eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake < 0) return errno;
  std::unique_lock lock(mutex_);
  if (fd_ >= 0 || closing_ || operations_) {
    lock.unlock(); ::close(wake); return EBUSY;
  }
  fd_ = fd; wake_ = wake; stopped_ = active_ = false;
  return 0;
}
void ControlDatagramChannel::activate() {
  std::lock_guard lock(mutex_);
  if (fd_ >= 0 && !closing_ && !stopped_) active_ = true;
}
void ControlDatagramChannel::deactivate() { std::lock_guard lock(mutex_); active_ = false; }
ControlDatagramChannel::SendLease ControlDatagramChannel::acquire(bool requireActive) {
  std::lock_guard lock(mutex_);
  if (fd_ < 0 || closing_ || stopped_ || (requireActive && !active_)) return {};
  ++operations_;
  return {this,fd_,wake_};
}
ControlDatagramChannel::SendLease ControlDatagramChannel::trySendLease() { return acquire(true); }
void ControlDatagramChannel::release() {
  std::lock_guard lock(mutex_);
  --operations_; released_.notify_all();
}
bool ControlDatagramChannel::stopped() { std::lock_guard lock(mutex_); return stopped_; }
void ControlDatagramChannel::deactivateAndRequestStop() {
  int wake;
  SendLease wakeLease;
  {
    std::lock_guard lock(mutex_);
    active_ = false; stopped_ = true;
    wake = wake_; if (wake < 0) return;
    ++operations_;
    wakeLease = SendLease(this,fd_,wake);
  }
  const uint64_t signal = 1;
  while (write(wake,&signal,sizeof(signal)) < 0 && errno == EINTR) {}
}
void ControlDatagramChannel::close() {
  int previous;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&previous);
  {
    std::unique_lock lock(mutex_);
    if (closing_) {
      released_.wait(lock,[this]{ return !closing_; });
    } else if (fd_ >= 0) {
      closing_ = true; active_ = false; stopped_ = true;
      const int wake = wake_;
      lock.unlock();
      const uint64_t signal = 1;
      while (write(wake,&signal,sizeof(signal)) < 0 && errno == EINTR) {}
      lock.lock();
      released_.wait(lock,[this]{ return operations_ == 0; });
      const int fd = std::exchange(fd_,-1);
      wake_ = -1;
      lock.unlock();
      ::close(fd); ::close(wake);
      lock.lock();
      closing_ = false; released_.notify_all();
    }
  }
  pthread_setcancelstate(previous,nullptr);
}
ControlDatagramChannel::~ControlDatagramChannel() { close(); }

int ControlDatagramChannel::SendLease::setResendTimeout() {
  const timeval timeout{0,100000};
  return setsockopt(fd_,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)) < 0 ? errno : 0;
}
ControlSendResult ControlDatagramChannel::SendLease::sendTo(std::span<const uint8_t> bytes,
                                                          const RemoteControlPeerSnapshot &peer) {
  const auto deadline = std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
  for (;;) {
    if (owner_->stopped()) return {-1,ECANCELED,true};
    const auto count = sendto(fd_,bytes.data(),bytes.size(),MSG_DONTWAIT,
                             reinterpret_cast<const sockaddr *>(&peer.address),peer.length);
    const int error = count < 0 ? errno : 0;
    if (count >= 0 || (error != EAGAIN && error != EWOULDBLOCK)) return {count,error};
    const auto remaining = deadline-std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) return {-1,EAGAIN};
    const int milliseconds = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
    pollfd ready[2]{{fd_,POLLOUT,0},{wake_,POLLIN,0}};
    const int result = poll(ready,2,milliseconds);
    if (result < 0) return {-1,errno};
    if (ready[1].revents) return {-1,ECANCELED,true};
    if (result == 0) return {-1,EAGAIN};
  }
}
ControlReceiveResult ControlDatagramChannel::receive(std::span<uint8_t> bytes) {
  auto lease = acquire(false);
  if (!lease) return {-1,0,true,{}};
  for (;;) {
    pollfd ready[2]{{lease.fd_,POLLIN,0},{lease.wake_,POLLIN,0}};
    if (poll(ready,2,-1) < 0) return {-1,errno,false,{}};
    if (ready[1].revents) return {-1,0,true,{}};
    ControlReceiveResult result;
    result.peer.length = sizeof(result.peer.address);
    result.count = recvfrom(lease.fd_,bytes.data(),bytes.size(),MSG_DONTWAIT,
                           reinterpret_cast<sockaddr *>(&result.peer.address),&result.peer.length);
    result.errorCode = result.count < 0 ? errno : 0;
    if (result.count < 0 && (result.errorCode == EAGAIN || result.errorCode == EWOULDBLOCK)) continue;
    return result;
  }
}

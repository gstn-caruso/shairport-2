#include "transport/event_port_worker.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <utility>

class EventWorkerFd {
public:
  explicit EventWorkerFd(int fd) : fd_(fd) {}
  ~EventWorkerFd() {
    if (fd_ >= 0) {
      int previous; pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&previous);
      close(fd_); pthread_setcancelstate(previous,nullptr);
    }
  }
  int release() { return std::exchange(fd_,-1); }
private:
  int fd_;
};

std::expected<void,int> EventPortWorker::start(int fd,EventCommandExchange &exchange,
                                            ChannelFactory factory,std::function<void()> initial) {
  EventWorkerFd listener(fd);
  if (!factory || !initial) return std::unexpected(EINVAL);
  if (listen(fd,128) < 0) return std::unexpected(errno);
  const int flags = fcntl(fd,F_GETFL),descriptorFlags = fcntl(fd,F_GETFD);
  if (flags < 0 || descriptorFlags < 0 || fcntl(fd,F_SETFL,flags | O_NONBLOCK) < 0 ||
      fcntl(fd,F_SETFD,descriptorFlags | FD_CLOEXEC) < 0) return std::unexpected(errno);
  const int wake = eventfd(0,EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake < 0) return std::unexpected(errno);
  EventWorkerFd wakeOwner(wake);
  std::lock_guard lock(mutex_);
  if (joining_ || thread_.joinable()) return std::unexpected(EALREADY);
  listener_ = fd; wake_ = wake; exchange_ = &exchange; stopping_ = false; error_ = 0;
  try {
    thread_ = std::jthread([this]{ run(); });
  } catch (const std::system_error &error) {
    listener_ = wake_ = -1; exchange_ = nullptr;
    return std::unexpected(error.code().value());
  }
  listener.release(); wakeOwner.release();
  return {};
}
void EventPortWorker::run() {
  pollfd ready[2]{{listener_,POLLIN,0},{wake_,POLLIN,0}};
  for (;;) {
    const int result = poll(ready,2,-1);
    if (result < 0 && errno == EINTR) continue;
    if (result < 0) { std::lock_guard lock(mutex_); error_ = errno; return; }
    if (ready[1].revents || ready[0].revents) return;
  }
}
int EventPortWorker::stop() {
  int previous; pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&previous);
  int result;
  {
    std::unique_lock lock(mutex_);
    if (joining_) joined_.wait(lock,[this]{ return !joining_; });
    else if (thread_.joinable()) {
      joining_ = stopping_ = true;
      const int wake = wake_,listener = listener_;
      auto *exchange = exchange_;
      lock.unlock();
      const uint64_t signal = 1;
      while (write(wake,&signal,sizeof(signal)) < 0 && errno == EINTR) {}
      thread_.join(); exchange->close();
      close(listener); close(wake);
      lock.lock();
      listener_ = wake_ = -1; exchange_ = nullptr; joining_ = false; joined_.notify_all();
    }
    result = error_;
  }
  pthread_setcancelstate(previous,nullptr);
  return result;
}
EventPortWorker::~EventPortWorker() { stop(); }

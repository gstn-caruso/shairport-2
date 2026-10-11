#include "protocol/ap2/native_event_channel.hpp"
#include "protocol/ap2/native_encrypted_channel.hpp"
#include <pthread.h>
#include <unistd.h>
#include <utility>
#include <stdexcept>
#include <fcntl.h>
#include <sys/socket.h>
#include <cerrno>

class AcceptedEventDescriptor {
public:
  explicit AcceptedEventDescriptor(int fd) : fd_(fd) {}
  AcceptedEventDescriptor(AcceptedEventDescriptor &&other) noexcept : fd_(std::exchange(other.fd_,-1)) {}
  ~AcceptedEventDescriptor() {
    if (fd_ < 0) return;
    int previous;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,&previous);
    close(fd_);
    pthread_setcancelstate(previous,nullptr);
  }
  int fd() const { return fd_; }
private:
  int fd_;
};
class NativeEventChannel final : public EventPlaintextChannel {
public:
  NativeEventChannel(AcceptedEventDescriptor fd,EncryptedChannel &cipher)
      : fd_(std::move(fd)),cipher_(cipher) {}
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    return write_encrypted(fd_.fd(),&cipher_,bytes.data(),bytes.size());
  }
  std::ptrdiff_t read(std::span<uint8_t> bytes) override {
    return read_encrypted(fd_.fd(),&cipher_,bytes.data(),bytes.size());
  }
private:
  AcceptedEventDescriptor fd_;
  EncryptedChannel &cipher_;
};
std::unique_ptr<EventPlaintextChannel> makeNativeEventChannel(int fd,EncryptedChannel &cipher) {
  if (fd < 0) throw std::invalid_argument("Event channel requires an accepted descriptor");
  AcceptedEventDescriptor accepted(fd);
  return std::make_unique<NativeEventChannel>(std::move(accepted),cipher);
}

class NativeEventInterrupt final : public EventChannelInterrupt {
public:
  explicit NativeEventInterrupt(AcceptedEventDescriptor fd) : fd_(std::move(fd)) {}
  void interrupt() noexcept override { shutdown(fd_.fd(),SHUT_RDWR); }
private:
  AcceptedEventDescriptor fd_;
};

std::expected<AcceptedEventChannel,int> makeInterruptibleNativeEventChannel(int fd,EncryptedChannel &cipher) {
  AcceptedEventDescriptor accepted(fd);
  const int duplicate = fcntl(fd,F_DUPFD_CLOEXEC,0);
  if (duplicate < 0) return std::unexpected(errno);
  AcceptedEventDescriptor interruptDescriptor(duplicate);
  try {
    auto interrupt = std::make_shared<NativeEventInterrupt>(std::move(interruptDescriptor));
    auto channel = std::make_unique<NativeEventChannel>(std::move(accepted),cipher);
    return AcceptedEventChannel{std::move(channel),std::move(interrupt)};
  } catch (const std::bad_alloc &) {
    return std::unexpected(ENOMEM);
  }
}

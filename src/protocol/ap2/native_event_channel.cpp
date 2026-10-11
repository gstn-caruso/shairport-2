#include "protocol/ap2/native_event_channel.hpp"
#include "protocol/rtsp/rtsp.h"
#include <pthread.h>
#include <unistd.h>
#include <utility>
#include <stdexcept>

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
  NativeEventChannel(AcceptedEventDescriptor fd,pair_cipher_bundle &cipher)
      : fd_(std::move(fd)),cipher_(cipher) {}
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override {
    return write_encrypted(fd_.fd(),&cipher_,bytes.data(),bytes.size());
  }
  std::ptrdiff_t read(std::span<uint8_t> bytes) override {
    return read_encrypted(fd_.fd(),&cipher_,bytes.data(),bytes.size());
  }
private:
  AcceptedEventDescriptor fd_;
  pair_cipher_bundle &cipher_;
};
std::unique_ptr<EventPlaintextChannel> makeNativeEventChannel(int fd,pair_cipher_bundle &cipher) {
  if (fd < 0) throw std::invalid_argument("Event channel requires an accepted descriptor");
  AcceptedEventDescriptor accepted(fd);
  return std::make_unique<NativeEventChannel>(std::move(accepted),cipher);
}

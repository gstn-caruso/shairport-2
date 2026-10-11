#include "protocol/ap2/native_encrypted_channel.hpp"
#include "transport/native_fd_byte_io.hpp"
#include "platform/utilities/debug.h"

class RuntimeCipherDiagnostics final : public CipherDiagnostics {
public:
  void decryptFailed(std::ptrdiff_t consumed) override {
    debug(1,"read_encrypted: abnormal exit from pair_decrypt: %zd.",consumed);
  }
  void encryptFailed(std::string_view message) override {
    debug(1,"%.*s",static_cast<int>(message.size()),message.data());
  }
};
std::ptrdiff_t read_encrypted(int fd, EncryptedChannel *channel, void *bytes, size_t count) {
  NativeFdByteIo io(fd); RuntimeCipherDiagnostics diagnostics;
  return channel->read(io,{static_cast<uint8_t *>(bytes),count},diagnostics);
}
std::ptrdiff_t write_encrypted(int fd, EncryptedChannel *channel, const void *bytes, size_t count) {
  NativeFdByteIo io(fd); RuntimeCipherDiagnostics diagnostics;
  return channel->write(io,{static_cast<const uint8_t *>(bytes),count},diagnostics);
}

#pragma once
#include "transport/encrypted_channel.hpp"
#include <unistd.h>

// Borrows the descriptor; its owner must keep it open for every operation.
class NativeFdByteIo final : public EncryptedByteIo {
public:
  explicit NativeFdByteIo(int fd) : fd_(fd) {}
  std::ptrdiff_t read(std::span<uint8_t> bytes) override { return ::read(fd_,bytes.data(),bytes.size()); }
  std::ptrdiff_t write(std::span<const uint8_t> bytes) override { return ::write(fd_,bytes.data(),bytes.size()); }
private:
  int fd_;
};

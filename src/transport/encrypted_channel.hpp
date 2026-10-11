#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <sys/types.h>
extern "C" {
#include "pair_ap/pair.h"
}

class EncryptedByteIo {
public:
  virtual ~EncryptedByteIo() = default;
  virtual std::ptrdiff_t read(std::span<uint8_t> bytes) = 0;
  virtual std::ptrdiff_t write(std::span<const uint8_t> bytes) = 0;
};
class CipherDiagnostics {
public:
  virtual ~CipherDiagnostics() = default;
  virtual void decryptFailed(std::ptrdiff_t consumed) = 0;
  virtual void encryptFailed(std::string_view message) = 0;
};

// Callers serialize IO and release; IO and diagnostic callbacks must not reenter.
class EncryptedChannel {
public:
  EncryptedChannel() = default;
  ~EncryptedChannel();
  EncryptedChannel(const EncryptedChannel &) = delete;
  EncryptedChannel &operator=(const EncryptedChannel &) = delete;
  EncryptedChannel(EncryptedChannel &&) = delete;
  EncryptedChannel &operator=(EncryptedChannel &&) = delete;
  bool prepare(pair_type type, int channel, std::span<const uint8_t> secret,
               const char *salt, std::string description);
  bool prepared() const { return cipher_ != nullptr; }
  void enableEncryptedResponses() { responsesEncrypted_ = true; }
  bool responsesEncrypted() const { return responsesEncrypted_; }
  std::ptrdiff_t read(EncryptedByteIo &io, std::span<uint8_t> bytes, CipherDiagnostics &diagnostics);
  std::ptrdiff_t write(EncryptedByteIo &io, std::span<const uint8_t> bytes, CipherDiagnostics &diagnostics);
  void release();
private:
  pair_cipher_context *cipher_ = nullptr;
  std::vector<uint8_t> encrypted_;
  std::vector<uint8_t> plaintext_;
  std::string description_;
  bool responsesEncrypted_ = false;
};

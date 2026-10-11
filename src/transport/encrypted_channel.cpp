#include "transport/encrypted_channel.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <utility>

EncryptedChannel::~EncryptedChannel() { release(); }
bool EncryptedChannel::prepare(pair_type type, int channel, std::span<const uint8_t> secret,
                               const char *salt, std::string description) {
  release();
  cipher_ = pair_cipher_new(type,channel,secret.data(),secret.size(),salt);
  if (!cipher_) return false;
  description_ = std::move(description);
  return true;
}
void EncryptedChannel::release() {
  std::vector<uint8_t>().swap(plaintext_);
  std::vector<uint8_t>().swap(encrypted_);
  std::string().swap(description_);
  responsesEncrypted_ = false;
  pair_cipher_free(std::exchange(cipher_,nullptr));
}
static size_t removePlaintext(std::vector<uint8_t> &plaintext, std::span<uint8_t> output) {
  const size_t count = std::min(plaintext.size(),output.size());
  std::copy_n(plaintext.begin(),count,output.begin());
  plaintext.erase(plaintext.begin(),plaintext.begin()+count);
  return count;
}
std::ptrdiff_t EncryptedChannel::read(EncryptedByteIo &io, std::span<uint8_t> bytes,
                                    CipherDiagnostics &diagnostics) {
  if (!plaintext_.empty()) return removePlaintext(plaintext_,bytes);
  std::array<uint8_t,4096> input;
  std::unique_ptr<uint8_t,decltype(&std::free)> plain(nullptr,std::free);
  size_t plainLength = 0;
  std::ptrdiff_t response;
  do {
    response = io.read(input);
    if (response > 0) {
      encrypted_.insert(encrypted_.end(),input.begin(),input.begin()+response);
      uint8_t *output = nullptr;
      const auto consumed = pair_decrypt(&output,&plainLength,encrypted_.data(),encrypted_.size(),cipher_);
      plain.reset(output);
      if (consumed < 0) {
        diagnostics.decryptFailed(consumed);
        return -1;
      }
      encrypted_.erase(encrypted_.begin(),encrypted_.begin()+consumed);
    }
  } while (plainLength == 0 && response > 0);
  if (response < 0) return response;
  if (plainLength) plaintext_.insert(plaintext_.end(),plain.get(),plain.get()+plainLength);
  return removePlaintext(plaintext_,bytes);
}
std::ptrdiff_t EncryptedChannel::write(EncryptedByteIo &io, std::span<const uint8_t> bytes,
                                     CipherDiagnostics &diagnostics) {
  uint8_t *output = nullptr;
  size_t length = 0;
  const auto result = pair_encrypt(&output,&length,bytes.data(),bytes.size(),cipher_);
  if (result < 0) {
    diagnostics.encryptFailed(pair_cipher_errmsg(cipher_));
    return -1;
  }
  std::unique_ptr<uint8_t,decltype(&std::free)> encrypted(output,std::free);
  size_t offset = 0;
  while (offset < length) {
    const auto written = io.write({encrypted.get()+offset,length-offset});
    if (written <= 0) return written;
    offset += written;
  }
  return bytes.size();
}

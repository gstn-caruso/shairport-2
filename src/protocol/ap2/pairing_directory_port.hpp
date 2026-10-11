#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct PairingRecord {
  std::string identifier;
  std::array<uint8_t,32> publicKey;
  bool operator==(const PairingRecord &) const = default;
};
class PairingDirectoryPort {
public:
  virtual ~PairingDirectoryPort() = default;
  virtual void upsert(std::string_view identifier,const std::array<uint8_t,32> &publicKey) = 0;
  virtual bool remove(std::string_view identifier) = 0;
  // Returned records own their data; consumers never run under the directory lock.
  virtual std::vector<PairingRecord> snapshot() const = 0;
};

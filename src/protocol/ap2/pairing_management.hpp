#pragma once
#include "protocol/ap2/pairing_directory_port.hpp"
#include <expected>
#include <span>

enum class PairingManagementError { invalidRequest,invalidPublicKey,allocationFailed };
class PairingManagement {
public:
  explicit PairingManagement(PairingDirectoryPort &directory) : directory_(directory) {}
  std::expected<std::vector<uint8_t>,PairingManagementError> add(std::span<const uint8_t> request);
  std::expected<std::vector<uint8_t>,PairingManagementError> remove(std::span<const uint8_t> request);
  std::expected<std::vector<uint8_t>,PairingManagementError> list(std::span<const uint8_t> request);
private:
  std::expected<std::vector<uint8_t>,PairingManagementError> change(std::span<const uint8_t> request,bool adding);
  PairingDirectoryPort &directory_;
};

#pragma once
#include <optional>
#include <string>

struct PairingOptions {
  std::string deviceId;
  std::optional<std::string> password;
};

#pragma once
#include "protocol/ap2/pairing_directory_port.hpp"
#include <memory>
#include "session/pairing_options.hpp"
std::shared_ptr<PairingDirectoryPort> makeTestPairingDirectory();
inline std::shared_ptr<const PairingOptions> makeTestPairingOptions() {
  return std::make_shared<const PairingOptions>(PairingOptions{"00:11:22:33:44:55",std::nullopt});
}

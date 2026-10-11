#pragma once
#include "protocol/ap2/pairing_directory_port.hpp"
#include <memory>
std::shared_ptr<PairingDirectoryPort> makeTestPairingDirectory();

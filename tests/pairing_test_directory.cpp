#include "pairing_test_directory.hpp"
import receiver.protocol.ap2.pairing_directory;
std::shared_ptr<PairingDirectoryPort> makeTestPairingDirectory() {
  return std::make_shared<PairingDirectory>();
}

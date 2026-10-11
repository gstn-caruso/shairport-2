module;
#include "protocol/ap2/pairing_directory_port.hpp"
#include <algorithm>
#include <mutex>
module receiver.protocol.ap2.pairing_directory;

void PairingDirectory::upsert(std::string_view identifier,const std::array<uint8_t,32> &publicKey) {
  std::lock_guard lock(mutex_);
  const auto found = std::find_if(records_.begin(),records_.end(),[&](const auto &record) {
    return record.identifier == identifier;
  });
  if (found != records_.end()) found->publicKey = publicKey;
  else records_.insert(records_.begin(),PairingRecord{std::string(identifier),publicKey});
}
bool PairingDirectory::remove(std::string_view identifier) {
  std::lock_guard lock(mutex_);
  const auto found = std::find_if(records_.begin(),records_.end(),[&](const auto &record) {
    return record.identifier == identifier;
  });
  if (found == records_.end()) return false;
  records_.erase(found); return true;
}
std::vector<PairingRecord> PairingDirectory::snapshot() const {
  std::lock_guard lock(mutex_); return records_;
}

module;
#include "protocol/ap2/pairing_directory_port.hpp"
#include <mutex>

export module receiver.protocol.ap2.pairing_directory;
export class PairingDirectory final : public PairingDirectoryPort {
public:
  void upsert(std::string_view identifier,const std::array<uint8_t,32> &publicKey) override;
  bool remove(std::string_view identifier) override;
  std::vector<PairingRecord> snapshot() const override;
private:
  mutable std::mutex mutex_;
  std::vector<PairingRecord> records_;
};

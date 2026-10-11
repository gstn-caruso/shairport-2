#include "protocol/ap2/pairing_management.hpp"
#include "platform/utilities/pairing_api.h"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>

namespace {
using Error = PairingManagementError;
struct Change {
  PairingDirectoryPort &directory;
  bool adding;
  std::optional<Error> error;
};
int changeRecord(uint8_t *key,const char *identifier,void *argument) {
  auto &operation = *static_cast<Change *>(argument);
  if (operation.adding && !key) { operation.error = Error::invalidPublicKey; return -1; }
  try {
    if (operation.adding) {
      std::array<uint8_t,32> publicKey; std::copy_n(key,publicKey.size(),publicKey.begin());
      operation.directory.upsert(identifier,publicKey);
    } else operation.directory.remove(identifier);
    return 0;
  } catch (const std::bad_alloc &) {
    operation.error = Error::allocationFailed; return -1;
  }
}
void enumerateRecords(pair_cb callback,void *callbackArgument,void *argument) {
  auto &records = *static_cast<std::vector<PairingRecord> *>(argument);
  for (auto &record : records) callback(record.publicKey.data(),record.identifier.c_str(),callbackArgument);
}
std::expected<std::vector<uint8_t>,Error> ownedResponse(int result,uint8_t *bytes,size_t length,
                                                     std::optional<Error> error = {}) {
  std::unique_ptr<uint8_t,decltype(&std::free)> response(bytes,std::free);
  if (error) return std::unexpected(*error);
  if (result < 0) return std::unexpected(Error::invalidRequest);
  try {
    if (!length) return std::vector<uint8_t>{};
    return std::vector<uint8_t>(bytes,bytes+length);
  } catch (const std::bad_alloc &) { return std::unexpected(Error::allocationFailed); }
}
}
std::expected<std::vector<uint8_t>,Error> PairingManagement::change(std::span<const uint8_t> request,bool adding) {
  Change operation{directory_,adding,{}};
  uint8_t *response = nullptr; size_t length = 0;
  const int result = adding
      ? pair_add(PAIR_SERVER_HOMEKIT,&response,&length,changeRecord,&operation,request.data(),request.size())
      : pair_remove(PAIR_SERVER_HOMEKIT,&response,&length,changeRecord,&operation,request.data(),request.size());
  return ownedResponse(result,response,length,operation.error);
}
std::expected<std::vector<uint8_t>,Error> PairingManagement::add(std::span<const uint8_t> request) {
  return change(request,true);
}
std::expected<std::vector<uint8_t>,Error> PairingManagement::remove(std::span<const uint8_t> request) {
  return change(request,false);
}
std::expected<std::vector<uint8_t>,Error> PairingManagement::list(std::span<const uint8_t> request) {
  try {
    auto records = directory_.snapshot();
    uint8_t *response = nullptr; size_t length = 0;
    const int result = pair_list(PAIR_SERVER_HOMEKIT,&response,&length,enumerateRecords,&records,request.data(),request.size());
    return ownedResponse(result,response,length);
  } catch (const std::bad_alloc &) { return std::unexpected(Error::allocationFailed); }
}

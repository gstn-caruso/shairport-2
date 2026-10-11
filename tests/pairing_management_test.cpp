#include "protocol/ap2/pairing_management.hpp"
#include <gtest/gtest.h>
#include "platform/utilities/pairing_api.h"
import receiver.protocol.ap2.pairing_directory;

static PairingDirectoryPort *mutateDuringList = nullptr;
extern "C" int __real_pair_list(enum pair_type,uint8_t **,size_t *,pair_list_cb,void *,const uint8_t *,size_t);
extern "C" int __wrap_pair_list(enum pair_type type,uint8_t **body,size_t *length,pair_list_cb callback,
                                void *argument,const uint8_t *request,size_t size) {
  if (mutateDuringList) mutateDuringList->remove("alpha");
  return __real_pair_list(type,body,length,callback,argument,request,size);
}

TEST(PairingManagement, EmptyListReturnsOwnedStateResponse) {
  PairingDirectory directory; PairingManagement management(directory);
  auto result = management.list({}); ASSERT_TRUE(result);
  EXPECT_EQ(*result,(std::vector<uint8_t>{6,1,2}));
}
static std::vector<uint8_t> request(std::string_view id,bool key = true) {
  std::vector<uint8_t> bytes{6,1,1,1,static_cast<uint8_t>(id.size())};
  bytes.insert(bytes.end(),id.begin(),id.end());
  if (key) { bytes.insert(bytes.end(),{3,32}); bytes.insert(bytes.end(),32,7); }
  return bytes;
}
TEST(PairingManagement, SnapshotSurvivesMutationDuringNativeEnumeration) {
  PairingDirectory directory; PairingManagement management(directory);
  ASSERT_TRUE(management.add(request("alpha")));
  mutateDuringList = &directory;
  auto result = management.list({}); mutateDuringList = nullptr;
  ASSERT_TRUE(result);
  std::vector<uint8_t> expected{6,1,2,1,5,'a','l','p','h','a',3,32};
  expected.insert(expected.end(),32,7); expected.insert(expected.end(),{11,1,1});
  EXPECT_EQ(*result,expected); EXPECT_TRUE(directory.snapshot().empty());
}
TEST(PairingManagement, MissingKeyDoesNotMutateAndMissingRemovalStillSucceeds) {
  PairingDirectory directory; PairingManagement management(directory);
  auto result = management.add(request("alpha",false));
  ASSERT_FALSE(result); EXPECT_EQ(result.error(),PairingManagementError::invalidPublicKey);
  EXPECT_TRUE(directory.snapshot().empty());
  auto removed = management.remove(request("absent",false)); ASSERT_TRUE(removed);
  EXPECT_EQ(*removed,(std::vector<uint8_t>{6,1,2}));
}
class FailingDirectory : public PairingDirectoryPort {
public:
  void upsert(std::string_view,const std::array<uint8_t,32> &) override { throw std::bad_alloc(); }
  bool remove(std::string_view) override { throw std::bad_alloc(); }
  std::vector<PairingRecord> snapshot() const override { throw std::bad_alloc(); }
};
TEST(PairingManagement, AllocationFailureStaysInsideNativeCallbackAndReturnsAnOwnedError) {
  FailingDirectory directory; PairingManagement management(directory);
  auto added = management.add(request("alpha"));
  ASSERT_FALSE(added); EXPECT_EQ(added.error(),PairingManagementError::allocationFailed);
  auto removed = management.remove(request("alpha",false));
  ASSERT_FALSE(removed); EXPECT_EQ(removed.error(),PairingManagementError::allocationFailed);
  auto listed = management.list({});
  ASSERT_FALSE(listed); EXPECT_EQ(listed.error(),PairingManagementError::allocationFailed);
}

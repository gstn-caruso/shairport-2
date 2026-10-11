#include "protocol/ap2/pairing_directory_port.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
import receiver.protocol.ap2.pairing_directory;

TEST(PairingDirectory, EmptySnapshotAndMissingRemoval) {
  PairingDirectory directory;
  EXPECT_TRUE(directory.snapshot().empty()); EXPECT_FALSE(directory.remove("absent"));
}
TEST(PairingDirectory, NewestFirstUpdateKeepsPositionAndSnapshotsOwnRecords) {
  PairingDirectory directory;
  std::array<uint8_t,32> first{},second{},replacement{};
  first.fill(1); second.fill(2); replacement.fill(3);
  directory.upsert("alpha",first); directory.upsert("beta",second); directory.upsert("alpha",replacement);
  auto snapshot = directory.snapshot();
  EXPECT_EQ(snapshot,(std::vector<PairingRecord>{{"beta",second},{"alpha",replacement}}));
  EXPECT_TRUE(directory.remove("beta")); EXPECT_TRUE(directory.remove("alpha"));
  EXPECT_TRUE(directory.snapshot().empty());
  EXPECT_EQ(snapshot,(std::vector<PairingRecord>{{"beta",second},{"alpha",replacement}}));
  for (const auto &record : snapshot) directory.upsert(record.identifier,record.publicKey);
  EXPECT_EQ(directory.snapshot(),(std::vector<PairingRecord>{{"alpha",replacement},{"beta",second}}));
}
TEST(PairingDirectory, ConcurrentUpdatesRemovalAndSnapshotsNeverExposePartialKeys) {
  PairingDirectory directory; std::atomic<bool> complete{true};
  std::vector<std::thread> workers;
  for (uint8_t worker = 1; worker <= 4; ++worker) workers.emplace_back([&,worker] {
    std::array<uint8_t,32> key; key.fill(worker);
    for (unsigned iteration = 0; iteration < 300; ++iteration) {
      directory.upsert("shared",key);
      for (const auto &record : directory.snapshot()) {
        for (uint8_t byte : record.publicKey) if (byte != record.publicKey.front()) complete = false;
      }
      if (iteration % 3 == 0) directory.remove("shared");
    }
  });
  for (auto &worker : workers) worker.join();
  EXPECT_TRUE(complete.load());
}

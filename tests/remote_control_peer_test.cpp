#include "transport/remote_control_peer.hpp"
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <thread>
#include <atomic>
#include <cstring>

TEST(RemoteControlPeer, CapturesFirstAddressByValueUntilCleared) {
  RemoteControlPeer peer;
  EXPECT_FALSE(peer.snapshot());
  sockaddr_in address{};
  address.sin_family = AF_INET; address.sin_port = htons(1234);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_TRUE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(address)));
  address.sin_port = htons(5678);
  EXPECT_FALSE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(address)));
  auto held = peer.snapshot();
  ASSERT_TRUE(held); EXPECT_EQ(held->length,sizeof(address));
  sockaddr_in copied{}; std::memcpy(&copied,&held->address,sizeof(copied));
  EXPECT_EQ(copied.sin_port,htons(1234));
  peer.clear();
  ASSERT_TRUE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(address)));
  EXPECT_EQ(copied.sin_port,htons(1234));
  auto replaced = peer.snapshot();
  ASSERT_TRUE(replaced);
  std::memcpy(&copied,&replaced->address,sizeof(copied));
  EXPECT_EQ(copied.sin_port,htons(5678));
}

TEST(RemoteControlPeer, ValidatesNativeAddressLengthsAndSupportsIpv6) {
  RemoteControlPeer peer;
  sockaddr_in6 address{}; address.sin6_family = AF_INET6;
  address.sin6_port = htons(1234); address.sin6_addr = in6addr_loopback;
  EXPECT_FALSE(peer.captureIfAbsent(nullptr,0));
  EXPECT_FALSE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(sa_family_t)-1));
  EXPECT_FALSE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(address)-1));
  EXPECT_FALSE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(sockaddr_storage)+1));
  address.sin6_family = AF_UNIX;
  EXPECT_FALSE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(address)));
  address.sin6_family = AF_INET6;
  ASSERT_TRUE(peer.captureIfAbsent(reinterpret_cast<sockaddr *>(&address),sizeof(address)));
  auto snapshot = peer.snapshot(); ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->length,sizeof(address));
  EXPECT_EQ(std::memcmp(&snapshot->address,&address,sizeof(address)),0);
}

TEST(RemoteControlPeer, ConcurrentCaptureClearAndSnapshotsRemainWholeOwnedValues) {
  RemoteControlPeer peer;
  sockaddr_in first{},second{};
  first.sin_family = second.sin_family = AF_INET;
  first.sin_port = htons(1234); second.sin_port = htons(5678);
  first.sin_addr.s_addr = htonl(0x7f000001); second.sin_addr.s_addr = htonl(0x7f000002);
  std::atomic<bool> valid{true};
  std::thread writer([&] {
    for (unsigned index = 0; index < 10000; ++index) {
      peer.clear();
      const auto &address = index%2 ? first : second;
      peer.captureIfAbsent(reinterpret_cast<const sockaddr *>(&address),sizeof(address));
    }
  });
  for (unsigned index = 0; index < 10000; ++index) {
    auto value = peer.snapshot();
    if (value && (value->length != sizeof(first) ||
        (std::memcmp(&value->address,&first,sizeof(first)) != 0 &&
         std::memcmp(&value->address,&second,sizeof(second)) != 0))) valid.store(false);
  }
  writer.join(); EXPECT_TRUE(valid.load());
}

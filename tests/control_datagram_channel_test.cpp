#include "transport/control_datagram_channel.hpp"
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <future>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

using namespace std::chrono_literals;
struct DatagramEndpoint {
  int fd = socket(AF_INET,SOCK_DGRAM | SOCK_CLOEXEC,0);
  RemoteControlPeerSnapshot peer;
  DatagramEndpoint() {
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
    peer.length = sizeof(address);
    EXPECT_EQ(getsockname(fd,reinterpret_cast<sockaddr *>(&peer.address),&peer.length),0);
  }
  ~DatagramEndpoint() { if (fd >= 0) ::close(fd); }
};

TEST(ControlDatagramChannel, RejectsInvalidAdoptionAndTransfersOnlySuccessfulDescriptors) {
  ControlDatagramChannel owner;
  EXPECT_EQ(owner.adopt(-1),EBADF);
  const int stream = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC,0);
  EXPECT_EQ(owner.adopt(stream),EINVAL); EXPECT_NE(fcntl(stream,F_GETFD),-1); ::close(stream);
  DatagramEndpoint first,second;
  const int descriptor = first.fd;
  ASSERT_EQ(owner.adopt(std::exchange(first.fd,-1)),0);
  EXPECT_FALSE(owner.trySendLease());
  EXPECT_EQ(owner.adopt(second.fd),EBUSY); EXPECT_NE(fcntl(second.fd,F_GETFD),-1);
  owner.activate(); ASSERT_TRUE(owner.trySendLease());
  owner.close(); owner.close(); EXPECT_EQ(fcntl(descriptor,F_GETFD),-1);
  ASSERT_EQ(owner.adopt(std::exchange(second.fd,-1)),0);
  owner.activate(); EXPECT_TRUE(owner.trySendLease());
}

TEST(ControlDatagramChannel, CloseWaitsForLeaseAndDescriptorRemainsValidUntilRelease) {
  DatagramEndpoint endpoint;
  ControlDatagramChannel owner;
  const int fd = endpoint.fd;
  ASSERT_EQ(owner.adopt(std::exchange(endpoint.fd,-1)),0); owner.activate();
  auto lease = owner.trySendLease(); ASSERT_TRUE(lease);
  auto closing = std::async(std::launch::async,[&]{ owner.close(); });
  EXPECT_EQ(closing.wait_for(20ms),std::future_status::timeout);
  EXPECT_NE(fcntl(fd,F_GETFD),-1);
  lease = {};
  ASSERT_EQ(closing.wait_for(500ms),std::future_status::ready); closing.get();
  EXPECT_EQ(fcntl(fd,F_GETFD),-1); EXPECT_FALSE(owner.trySendLease());
}

TEST(ControlDatagramChannel, ReceiveBeforeActivationPreservesBytesAndNativePeer) {
  DatagramEndpoint receiver,sender;
  ControlDatagramChannel owner;
  ASSERT_EQ(owner.adopt(std::exchange(receiver.fd,-1)),0);
  std::array<uint8_t,8> bytes{};
  auto receiving = std::async(std::launch::async,[&]{ return owner.receive(bytes); });
  const std::array<uint8_t,4> packet{1,2,3,4};
  ASSERT_EQ(sendto(sender.fd,packet.data(),packet.size(),0,
      reinterpret_cast<sockaddr *>(&receiver.peer.address),receiver.peer.length),4);
  ASSERT_EQ(receiving.wait_for(500ms),std::future_status::ready);
  auto result = receiving.get(); EXPECT_EQ(result.count,4); EXPECT_EQ(result.errorCode,0);
  EXPECT_FALSE(result.stopped); EXPECT_EQ(result.peer.length,sender.peer.length);
  EXPECT_EQ(std::memcmp(&result.peer.address,&sender.peer.address,result.peer.length),0);
  EXPECT_EQ(std::span<const uint8_t>(bytes).first(4)[3],4);
  EXPECT_FALSE(owner.trySendLease());
}

TEST(ControlDatagramChannel, StopWakesIndefiniteReceiveWithoutFabricatingTimeout) {
  DatagramEndpoint endpoint;
  ControlDatagramChannel owner;
  ASSERT_EQ(owner.adopt(std::exchange(endpoint.fd,-1)),0);
  std::array<uint8_t,8> bytes{};
  auto receiving = std::async(std::launch::async,[&]{ return owner.receive(bytes); });
  EXPECT_EQ(receiving.wait_for(20ms),std::future_status::timeout);
  owner.deactivateAndRequestStop();
  ASSERT_EQ(receiving.wait_for(500ms),std::future_status::ready);
  auto result = receiving.get(); EXPECT_TRUE(result.stopped); EXPECT_EQ(result.errorCode,0);
  owner.close();
}

TEST(ControlDatagramChannel, SendAndReceiveUseIndependentLeases) {
  DatagramEndpoint receiver,peer;
  ControlDatagramChannel owner;
  ASSERT_EQ(owner.adopt(std::exchange(receiver.fd,-1)),0); owner.activate();
  std::array<uint8_t,8> input{};
  auto receiving = std::async(std::launch::async,[&]{ return owner.receive(input); });
  auto lease = owner.trySendLease(); ASSERT_TRUE(lease);
  EXPECT_EQ(lease.setResendTimeout(),0);
  const std::array<uint8_t,8> request{0x80,0xd5,0,1,0,2,0,3};
  EXPECT_EQ(lease.sendTo(request,peer.peer).count,8);
  EXPECT_EQ(recv(peer.fd,input.data(),input.size(),MSG_DONTWAIT),8);
  owner.deactivateAndRequestStop();
  ASSERT_EQ(receiving.wait_for(500ms),std::future_status::ready); EXPECT_TRUE(receiving.get().stopped);
  lease = {};
}

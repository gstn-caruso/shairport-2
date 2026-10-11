#include "transport/event_port_worker.hpp"
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <future>
#include <chrono>

struct BoundEventListener {
  int fd = socket(AF_INET,SOCK_STREAM | SOCK_CLOEXEC,0);
  sockaddr_in address{};
  BoundEventListener() {
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    EXPECT_EQ(bind(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0);
    socklen_t size = sizeof(address);
    EXPECT_EQ(getsockname(fd,reinterpret_cast<sockaddr *>(&address),&size),0);
  }
  ~BoundEventListener() { if (fd >= 0) close(fd); }
};

TEST(EventPortWorker, StopBeforeAcceptWakesAndClosesOwnedListener) {
  BoundEventListener listener;
  const int fd = listener.fd;
  EventCommandExchange exchange;
  EventPortWorker worker;
  unsigned calls = 0;
  ASSERT_TRUE(worker.start(std::exchange(listener.fd,-1),exchange,
      [](int)->std::expected<AcceptedEventChannel,int> { return std::unexpected(EINVAL); },[&]{ ++calls; }));
  auto stopping = std::async(std::launch::async,[&]{ return worker.stop(); });
  ASSERT_EQ(stopping.wait_for(std::chrono::milliseconds(500)),std::future_status::ready);
  EXPECT_EQ(stopping.get(),0); EXPECT_EQ(fcntl(fd,F_GETFD),-1); EXPECT_EQ(calls,0u);
  EXPECT_EQ(worker.stop(),0);
}

TEST(EventPortWorker, StartupConsumesInvalidOrUnlistenableDescriptorOnFailure) {
  EventCommandExchange exchange;
  EventPortWorker worker;
  auto factory = [](int)->std::expected<AcceptedEventChannel,int> { return std::unexpected(EINVAL); };
  EXPECT_FALSE(worker.start(-1,exchange,factory,[]{}));
  const int fd = socket(AF_INET,SOCK_DGRAM,0);
  ASSERT_GE(fd,0);
  EXPECT_FALSE(worker.start(fd,exchange,factory,[]{}));
  EXPECT_EQ(fcntl(fd,F_GETFD),-1);
  EXPECT_EQ(worker.stop(),0);
}

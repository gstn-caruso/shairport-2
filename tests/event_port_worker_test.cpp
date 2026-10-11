#include "transport/event_port_worker.hpp"
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

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

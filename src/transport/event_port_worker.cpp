#include "transport/event_port_worker.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

std::expected<void,int> EventPortWorker::start(int fd,EventCommandExchange &,
                                            ChannelFactory,std::function<void()>) {
  const int result = listen(fd,128);
  const int error = result < 0 ? errno : ENOSYS;
  if (fd >= 0) close(fd);
  return std::unexpected(error);
}
int EventPortWorker::stop() { return 0; }
EventPortWorker::~EventPortWorker() { stop(); }

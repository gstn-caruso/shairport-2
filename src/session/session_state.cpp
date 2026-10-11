#include "session/session_state.hpp"
#include <unistd.h>
#include <stdexcept>

std::unique_ptr<SessionState> SessionState::create(std::shared_ptr<SharedRtpTimingSettings> timing) {
  if (!timing)
    throw std::invalid_argument("A session requires shared RTP timing settings");
  auto session = std::make_unique<SessionState>();
  session->timingSettings_ = std::move(timing);
  return session;
}

SessionState::~SessionState() {
  playbackRun.stop();
  if (fd >= 0)
    close(fd);
}

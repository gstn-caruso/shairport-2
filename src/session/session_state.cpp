#include "session/session_state.hpp"
#include <unistd.h>
#include <stdexcept>

std::unique_ptr<SessionState> SessionState::create(std::shared_ptr<SharedRtpTimingSettings> timing,
                                                std::shared_ptr<PairingDirectoryPort> directory,
                                                std::shared_ptr<const PairingOptions> options) {
  if (!timing || !directory || !options)
    throw std::invalid_argument("A session requires shared timing, pairing directory and pairing options");
  auto session = std::make_unique<SessionState>();
  session->timingSettings_ = std::move(timing);
  session->pairingDirectory_ = std::move(directory);
  session->pairingSession.configure(std::move(options));
  return session;
}

SessionState::~SessionState() {
  eventPortWorker.stop();
  eventCommandExchange.close();
  playbackRun.stop();
  if (fd >= 0)
    close(fd);
}

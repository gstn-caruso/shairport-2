#pragma once
#include <mutex>
#include <optional>
#include <sys/socket.h>

struct RemoteControlPeerSnapshot {
  sockaddr_storage address{};
  socklen_t length = 0;
};

class RemoteControlPeer {
public:
  // The caller supplies length readable native-address bytes; snapshots own their copy.
  bool captureIfAbsent(const sockaddr *address,socklen_t length);
  std::optional<RemoteControlPeerSnapshot> snapshot() const;
  void clear();

private:
  mutable std::mutex mutex_;
  std::optional<RemoteControlPeerSnapshot> peer_;
};

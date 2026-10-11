#include "transport/remote_control_peer.hpp"
#include <netinet/in.h>
#include <cstring>

bool RemoteControlPeer::captureIfAbsent(const sockaddr *address,socklen_t length) {
  if (!address || length < sizeof(sa_family_t) || length > sizeof(sockaddr_storage))
    return false;
  sa_family_t family;
  std::memcpy(&family,address,sizeof(family));
  if ((family != AF_INET && family != AF_INET6) ||
      length < (family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6)))
    return false;
  std::lock_guard lock(mutex_);
  if (peer_) return false;
  RemoteControlPeerSnapshot value;
  value.length = length;
  std::memcpy(&value.address,address,length);
  peer_ = value;
  return true;
}

std::optional<RemoteControlPeerSnapshot> RemoteControlPeer::snapshot() const {
  std::lock_guard lock(mutex_);
  return peer_;
}

void RemoteControlPeer::clear() {
  std::lock_guard lock(mutex_);
  peer_.reset();
}

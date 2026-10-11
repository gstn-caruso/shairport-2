#pragma once
#include "protocol/ap2/event_command_exchange.hpp"
struct pair_cipher_bundle;

// Takes ownership of an accepted fd, including zero; cipher must outlive channel IO.
std::unique_ptr<EventPlaintextChannel> makeNativeEventChannel(int fd,pair_cipher_bundle &cipher);

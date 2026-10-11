#pragma once
#include "protocol/ap2/event_command_exchange.hpp"
#include "transport/event_port_worker.hpp"
struct pair_cipher_bundle;

// Takes ownership of an accepted fd, including zero; cipher must outlive channel IO.
std::unique_ptr<EventPlaintextChannel> makeNativeEventChannel(int fd,pair_cipher_bundle &cipher);
// Consumes fd on failure too. The interrupt keeps the socket alive independently of channel close.
std::expected<AcceptedEventChannel,int> makeInterruptibleNativeEventChannel(int fd,pair_cipher_bundle &cipher);

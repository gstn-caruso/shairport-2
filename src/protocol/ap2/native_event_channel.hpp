#pragma once
#include "protocol/ap2/event_command_exchange.hpp"
#include "transport/event_port_worker.hpp"
#include "transport/encrypted_channel.hpp"

// Takes ownership of an accepted fd, including zero; cipher must outlive channel IO.
std::unique_ptr<EventPlaintextChannel> makeNativeEventChannel(int fd,EncryptedChannel &cipher);
// Consumes fd on failure too. The interrupt keeps the socket alive independently of channel close.
std::expected<AcceptedEventChannel,int> makeInterruptibleNativeEventChannel(int fd,EncryptedChannel &cipher);

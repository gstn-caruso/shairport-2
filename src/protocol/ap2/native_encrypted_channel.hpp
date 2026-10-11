#pragma once
#include "transport/encrypted_channel.hpp"

std::ptrdiff_t read_encrypted(int fd, EncryptedChannel *channel, void *bytes, size_t count);
std::ptrdiff_t write_encrypted(int fd, EncryptedChannel *channel, const void *bytes, size_t count);

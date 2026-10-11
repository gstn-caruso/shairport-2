#pragma once
#include "transport/encrypted_channel.hpp"

struct ap2_pairing {
  pair_setup_context *setup_ctx = nullptr;
  pair_verify_context *verify_ctx = nullptr;
  EncryptedChannel control_cipher_bundle;
  EncryptedChannel event_cipher_bundle;
  EncryptedChannel data_cipher_bundle;
  char *data_cipher_salt = nullptr;
};

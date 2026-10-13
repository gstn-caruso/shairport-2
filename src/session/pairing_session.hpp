#pragma once
#include "session/pairing_options.hpp"
#include "transport/encrypted_channel.hpp"
#include <memory>
#include <vector>

enum class PairingDiagnostic {
  none, unconfigured, setupCreationFailed, verifyCreationFailed,
  setupFailed, verifyFailed, controlCipherFailed, eventCipherFailed
};
struct PairingReply {
  int status = 200;
  std::vector<uint8_t> body;
  bool bodyPresent = false;
  PairingDiagnostic diagnostic = PairingDiagnostic::none;
  std::string error;
};
enum class DataPreparation { prepared, noSetupSecret, cipherFailed };

/**
 * @brief Owns one receiver session's pairing exchanges and encrypted channels.
 *
 * Setup delegates the HomeKit exchange to a native setup context, then uses its
 * completed shared secret to prepare the control and event channels. The data
 * channel additionally uses the stream seed as its salt. Verify delegates to a
 * separate native verification context. Replies copy native response storage
 * into owned bytes before returning to the protocol adapter.
 *
 * The setup result is borrowed from the setup context. Channel references are
 * borrowed from this session. Configure releases the previous exchanges and
 * channels before replacing the options; release clears them while retaining
 * the configured options.
 *
 * @note Callers must serialize setup, verify, configure, channel preparation,
 * channel IO and release. Stop and join channel users before reconfiguration,
 * release or destruction; borrowed channel references must not outlive this
 * owner. This object provides no internal synchronization.
 * @see EncryptedChannel
 */
class PairingSession {
public:
  PairingSession() = default;
  ~PairingSession();
  void configure(std::shared_ptr<const PairingOptions> options);
  const std::shared_ptr<const PairingOptions> &options() const { return options_; }
  PairingReply setup(std::span<const uint8_t> request);
  PairingReply verify(std::span<const uint8_t> request);
  DataPreparation prepareDataChannel(uint64_t seed);
  EncryptedChannel &controlChannel() { return control_; }
  EncryptedChannel &eventChannel() { return event_; }
  EncryptedChannel &dataChannel() { return data_; }
  void release();
private:
  std::shared_ptr<const PairingOptions> options_;
  std::unique_ptr<pair_setup_context,decltype(&pair_setup_free)> setup_{nullptr,pair_setup_free};
  std::unique_ptr<pair_verify_context,decltype(&pair_verify_free)> verify_{nullptr,pair_verify_free};
  pair_result *setupResult_ = nullptr;
  EncryptedChannel control_,event_,data_;
};

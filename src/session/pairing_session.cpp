#include "session/pairing_session.hpp"
#include <cstdlib>
#include <utility>

static PairingReply copyReply(int result, uint8_t *body, size_t length,
                              PairingDiagnostic failure, const char *error) {
  std::unique_ptr<uint8_t,decltype(&std::free)> owned(body,std::free);
  PairingReply reply;
  reply.bodyPresent = body != nullptr;
  if (body && length) reply.body.assign(body,body+length);
  if (result < 0) {
    reply.status = 470;
    reply.diagnostic = failure;
    if (error) reply.error = error;
  }
  return reply;
}
PairingSession::~PairingSession() { release(); }
void PairingSession::configure(std::shared_ptr<const PairingOptions> options) {
  release(); options_ = std::move(options);
}
void PairingSession::release() {
  setupResult_ = nullptr;
  control_.release(); event_.release(); data_.release();
  setup_.reset(); verify_.reset();
}
PairingReply PairingSession::setup(std::span<const uint8_t> request) {
  if (!options_) return {500,{},false,PairingDiagnostic::unconfigured,{}};
  if (!setup_) {
    const char *password = options_->password ? options_->password->c_str() : nullptr;
    setup_.reset(pair_setup_new(PAIR_SERVER_HOMEKIT,password,nullptr,nullptr,options_->deviceId.c_str()));
    if (!setup_) return {500,{},false,PairingDiagnostic::setupCreationFailed,{}};
  }
  uint8_t *body = nullptr; size_t length = 0;
  const int result = pair_setup(&body,&length,setup_.get(),request.data(),request.size());
  auto reply = copyReply(result,body,length,PairingDiagnostic::setupFailed,pair_setup_errmsg(setup_.get()));
  if (result < 0) return reply;
  if (pair_setup_result(nullptr,&setupResult_,setup_.get()) == 0 &&
      setupResult_ && setupResult_->shared_secret_len > 0) {
    const std::span<const uint8_t> secret{setupResult_->shared_secret,setupResult_->shared_secret_len};
    if (!control_.prepare(PAIR_SERVER_HOMEKIT,3,secret,"","Control Stream"))
      reply.diagnostic = PairingDiagnostic::controlCipherFailed;
    else if (!event_.prepare(PAIR_SERVER_HOMEKIT,4,secret,"","Event Stream"))
      reply.diagnostic = PairingDiagnostic::eventCipherFailed;
  }
  return reply;
}
PairingReply PairingSession::verify(std::span<const uint8_t> request) {
  if (!options_) return {500,{},false,PairingDiagnostic::unconfigured,{}};
  if (!verify_) {
    verify_.reset(pair_verify_new(PAIR_SERVER_HOMEKIT,nullptr,nullptr,nullptr,options_->deviceId.c_str()));
    if (!verify_) return {500,{},false,PairingDiagnostic::verifyCreationFailed,{}};
  }
  uint8_t *body = nullptr; size_t length = 0;
  const int result = pair_verify(&body,&length,verify_.get(),request.data(),request.size());
  return copyReply(result,body,length,PairingDiagnostic::verifyFailed,pair_verify_errmsg(verify_.get()));
}
DataPreparation PairingSession::prepareDataChannel(uint64_t seed) {
  if (!setupResult_ || setupResult_->shared_secret_len == 0) return DataPreparation::noSetupSecret;
  const auto salt = std::to_string(seed);
  return data_.prepare(PAIR_SERVER_HOMEKIT,5,
      {setupResult_->shared_secret,setupResult_->shared_secret_len},salt.c_str(),"DataStream")
      ? DataPreparation::prepared : DataPreparation::cipherFailed;
}

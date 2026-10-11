/*
 * Apple RTP protocol handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Copyright (c) Mike Brady 2014--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "session/session_state.hpp"
#include <bit>
#include "protocol/rtp/rtp.h"
#include "runtime/common.h"
#include "playback/player.h"
#include "protocol/rtsp/rtsp.h"
#include "platform/utilities/network_utilities.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// #include "plist_xml_strings.h"
#include "timing/ptp-utilities.h"
#include "platform/utilities/structured_buffer.h"
#include "platform/utilities/ffmpeg_api.h"
#include <span>
#include <functional>
#include <limits>
import receiver.protocol.ap2.realtime_audio;
import receiver.protocol.ap2.control_packet;



/*
      char obf[4096];
      char *obfp = obf;
      size_t obfc;
      for (obfc=0; obfc < strlen(buffer); obfc++) {
        snprintf(obfp, 3, "%02X", buffer[obfc]);
        obfp+=2;
      };
      *obfp=0;
      debug(1,"Writing: \"%s\"",obf);

*/

void check64conversion(const char *prompt, const uint8_t *source, uint64_t value) {
  char converted_value[128];
  sprintf(converted_value, "%" PRIx64 "", value);

  char obf[32];
  char *obfp = obf;
  int obfc;
  int suppress_zeroes = 1;
  for (obfc = 0; obfc < 8; obfc++) {
    if ((suppress_zeroes == 0) || (source[obfc] != 0)) {
      if (suppress_zeroes != 0) {
        if (source[obfc] < 0x10) {
          snprintf(obfp, 3, "%1x", source[obfc]);
          obfp += 1;
        } else {
          snprintf(obfp, 3, "%02x", source[obfc]);
          obfp += 2;
        }
      } else {
        snprintf(obfp, 3, "%02x", source[obfc]);
        obfp += 2;
      }
      suppress_zeroes = 0;
    }
  };
  *obfp = 0;
  if (strcmp(converted_value, obf) != 0) {
    debug(1, "%s check64conversion error converting \"%s\" to %" PRIx64 ".", prompt, obf, value);
  }
}

void check32conversion(const char *prompt, const uint8_t *source, uint32_t value) {
  char converted_value[128];
  sprintf(converted_value, "%" PRIx32 "", value);

  char obf[32];
  char *obfp = obf;
  int obfc;
  int suppress_zeroes = 1;
  for (obfc = 0; obfc < 4; obfc++) {
    if ((suppress_zeroes == 0) || (source[obfc] != 0)) {
      if (suppress_zeroes != 0) {
        if (source[obfc] < 0x10) {
          snprintf(obfp, 3, "%1x", source[obfc]);
          obfp += 1;
        } else {
          snprintf(obfp, 3, "%02x", source[obfc]);
          obfp += 2;
        }
      } else {
        snprintf(obfp, 3, "%02x", source[obfc]);
        obfp += 2;
      }
      suppress_zeroes = 0;
    }
  };
  *obfp = 0;
  if (strcmp(converted_value, obf) != 0) {
    debug(1, "%s check32conversion error converting \"%s\" to %" PRIx32 ".", prompt, obf, value);
  }
}

void rtp_initialise(rtsp_conn_info *conn) {
  conn->resendCoordinator.reset();
  conn->controlChannel.deactivate();
  // initialise the timer mutex
  int rc = pthread_mutex_init(&conn->reference_time_mutex, NULL);
  if (rc)
    debug(1, "Error initialising reference_time_mutex.");
}

void rtp_terminate(rtsp_conn_info *conn) {
  conn->clock.reset();
  // destroy the timer mutex
  int rc = pthread_mutex_destroy(&conn->reference_time_mutex);
  if (rc)
    debug(1, "Error destroying reference_time_mutex variable.");
}

class NativeResendIo final : public ResendIo {
public:
  NativeResendIo(ControlDatagramChannel::SendLease &lease,RemoteControlPeerSnapshot peer) : lease_(lease),peer_(peer) {}
  uint64_t nowNs() override { return get_absolute_time_in_ns(); }
  double draw() override { return drand48(); }
  ResendSendResult send(std::span<const uint8_t,8> packet) override {
    const int timeoutError = lease_.setResendTimeout();
    if (timeoutError) {
      debug(1,"Can't set timeout on resend request socket.");
    }
    const auto sent = lease_.sendTo(packet,peer_);
    return {sent.count,sent.errorCode,timeoutError};
  }
private:
  ControlDatagramChannel::SendLease &lease_;
  RemoteControlPeerSnapshot peer_;
};

void rtp_request_resend(seq_t first,uint32_t count,rtsp_conn_info *conn) {
  auto lease = conn->controlChannel.trySendLease();
  if (!lease) {
    debug(2,"rtp_request_resend called without active stream!");
    return;
  }
  const auto peer = conn->remoteControlPeer.snapshot();
  if (!peer) {
    debug(2,"No remote socket -- skipping the resend");
    return;
  }
  NativeResendIo io(lease,*peer);
  const auto outcome = conn->resendCoordinator.request(
      true,true,first,count,config.diagnostic_drop_packet_fraction,io);
  switch (outcome.status) {
  case ResendStatus::sendFailed: {
    char message[1024];
    strerror_r(outcome.errorCode,message,sizeof(message));
    debug(2,"Error %d using sendto to request a resend: \"%s\".",outcome.errorCode,message);
    break;
  }
  case ResendStatus::dropped:
    debug(3,"Dropping resend request packet to simulate a bad network. Backing off for 0.3 second.");
    break;
  case ResendStatus::suppressed:
    debug(1,"Suppressing a resend request due to a resend sendto error in the last 0.3 seconds.");
    break;
  case ResendStatus::inactive:
  case ResendStatus::noPeer:
  case ResendStatus::sent:
    break;
  }
}


void set_ptp_anchor_info(rtsp_conn_info *conn, uint64_t clock_id, uint32_t rtptime,
                         uint64_t networktime) {
  conn->clock.setAnchor(clock_id, rtptime, networktime, get_absolute_time_in_ns());
  debug(2, "Connection %d: Set Anchor Clock: %" PRIx64 ", rtptime: %u, networktime: %" PRIu64 ".",
        conn->connection_number, clock_id, rtptime, networktime);
}

class ClockDiagnostics {
public:
  void reset() noexcept {
    oldSampleNotification_ = false;
    previousOffset_ = 0;
    previousClock_ = 0;
  }

  void observe(const ClockSample &sample, uint64_t now) {
    auto mastershipAge = std::bit_cast<int64_t>(now - sample.mastershipStart);
    if (sample.status != clock_ok || mastershipAge < 400000000)
      return;
    auto sampleAge = std::bit_cast<int64_t>(now - sample.sampleTime);
    if (sampleAge > 300000000000 && !oldSampleNotification_) {
      debug(1, "The last PTP timing sample is pretty old: %f seconds.", sampleAge * 0.000000001);
      oldSampleNotification_ = true;
    } else if (sampleAge < 2000000000 && oldSampleNotification_) {
      debug(1, "The last PTP timing sample is no longer too old: %f seconds.", sampleAge * 0.000000001);
      oldSampleNotification_ = false;
    }
    auto jitter = std::bit_cast<int64_t>(sample.offset - previousOffset_);
    if (previousOffset_ != 0 && previousClock_ == sample.id &&
        (jitter > 3000000 || jitter < -3000000))
      debug(1, "Clock jitter: %.3f mS. Time since sample: %.3f mS. Time since start of mastership: %.3f seconds.",
            jitter * 0.000001, sampleAge * 0.000001, mastershipAge * 0.000000001);
    previousOffset_ = sample.offset;
    previousClock_ = sample.id;
  }

private:
  bool oldSampleNotification_ = false;
  uint64_t previousOffset_ = 0;
  uint64_t previousClock_ = 0;
};

static ClockDiagnostics clockDiagnostics;

void reset_ptp_anchor_info(rtsp_conn_info *conn) {
  debug(2, "Connection %d: Clear anchor information.", conn->connection_number);
  conn->clock.reset();
  clockDiagnostics.reset();
}

int get_ptp_anchor_local_time_info(rtsp_conn_info *conn, uint32_t *anchorRTP,
                                  uint64_t *anchorLocalTime) {
  if (!conn->clock.hasAnchor())
    return clock_no_anchor_info;
  auto previousStatus = conn->clock.status();
  ClockSample sample{};
  sample.status = static_cast<clock_status_t>(
      ptp_get_clock_info(&sample.id, &sample.sampleTime, &sample.offset, &sample.mastershipStart));
  auto now = get_absolute_time_in_ns();
  int response = conn->clock.observe(sample, now);
  clockDiagnostics.observe(sample, now);
  auto actual_clock_id = sample.id;
  if (response != previousStatus) {
      switch (response) {
      case clock_ok:
        debug(2, "Connection %d: NQPTP master clock %" PRIx64 ".", conn->connection_number,
              actual_clock_id);
        break;
      case clock_not_ready:
        debug(2, "Connection %d: NQPTP master clock %" PRIx64 " is available but not ready.",
              conn->connection_number, actual_clock_id);
        break;
      case clock_service_unavailable:
        debug(1, "Connection %d: NQPTP clock is not available.", conn->connection_number);
        warn("Can't access the NQPTP clock. Is NQPTP running?");
        break;
      case clock_access_error:
        debug(2, "Connection %d: Error accessing the NQPTP clock interface.",
              conn->connection_number);
        break;
      case clock_data_unavailable:
        debug(1, "Connection %d: Can not access NQPTP clock information.", conn->connection_number);
        break;
      case clock_no_master:
        debug(2, "Connection %d: No NQPTP master clock.", conn->connection_number);
        break;
      case clock_no_anchor_info:
        debug(2, "Connection %d: Awaiting clock anchor information.", conn->connection_number);
        break;
      case clock_version_mismatch:
        debug(2, "Connection %d: NQPTP clock interface mismatch.", conn->connection_number);
        warn(
            "This version of Shairport Sync is not compatible with the installed version of NQPTP. "
            "Please update.");
        break;
      case clock_not_synchronised:
        debug(1, "Connection %d: NQPTP clock is not synchronised.", conn->connection_number);
        break;
      case clock_not_valid:
        debug(2, "Connection %d: NQPTP clock information is not valid.", conn->connection_number);
        break;
      default:
        debug(1, "Connection %d: NQPTP clock reports an unrecognised status: %u.",
              conn->connection_number, response);
        break;
      }
  }
  if (auto frame = conn->clock.anchorFrame(conn->inputAudio.sampleRate(), conn->timingSettings().offsetSeconds());
      frame && anchorRTP)
    *anchorRTP = *frame;
  if (auto time = conn->clock.localAnchorTime(); time && anchorLocalTime)
    *anchorLocalTime = *time;
  return response;
}

int have_ptp_timing_information(rtsp_conn_info *conn) {
  return get_ptp_anchor_local_time_info(conn, NULL, NULL) == clock_ok;
}

int frame_to_ptp_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  if (conn->inputAudio.sampleRate() == 0)
    return -1;
  get_ptp_anchor_local_time_info(conn, NULL, NULL);
  auto converted = conn->clock.localTimeForFrame(timestamp, conn->inputAudio.sampleRate(),
                                                conn->timingSettings().offsetSeconds());
  if (!converted)
    return -1;
  *time = *converted;
  return 0;
}

int local_ptp_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  if (conn->inputAudio.sampleRate() == 0)
    return -1;
  get_ptp_anchor_local_time_info(conn, NULL, NULL);
  auto converted = conn->clock.frameForLocalTime(time, conn->inputAudio.sampleRate(),
                                                conn->timingSettings().offsetSeconds());
  if (!converted)
    return -1;
  *frame = *converted;
  return 0;
}

void rtp_ap2_control_handler_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Control Receiver Cleanup.", conn->connection_number);
  conn->controlChannel.close();
  debug(2, "Connection %d: UDP control port %u closed.", conn->connection_number,
        conn->local_ap2_control_port);
  conn->remoteControlPeer.clear();
}

static std::span<const uint8_t> realtimeSessionKey(const SessionState &session) {
  return session.session_key ? std::span<const uint8_t>(session.session_key,32) : std::span<const uint8_t>{};
}
static void submitRealtimeAudio(AuthenticatedRealtimeAudio &audio, SessionState &session) {
  player_put_packet(ALAC_44100_S16_2,audio.sequence,audio.timestamp,audio.plaintext.data(),
                    audio.plaintext.size(),0,0,&session);
}
static void reportRealtimeAudioError(RealtimeAudioError error) {
  switch (error) {
  case RealtimeAudioError::authenticationFailed:
    debug(1,"Error decrypting an audio packet.");
    break;
  case RealtimeAudioError::missingKey:
    debug(2,"No session key, so the audio packet can not be deciphered -- skipped.");
    break;
  case RealtimeAudioError::invalidKeySize:
    debug(1,"Invalid realtime audio key length -- ignored.");
    break;
  case RealtimeAudioError::tooShort:
    debug(1,"packet was too small -- ignored");
    break;
  case RealtimeAudioError::tooLarge:
    debug(1,"packet was too large -- ignored");
    break;
  }
}
void decipher_player_put_packet(uint8_t *packet, ssize_t count, rtsp_conn_info *conn) {
  if (count < 0) {
    reportRealtimeAudioError(RealtimeAudioError::tooShort);
    return;
  }
  auto audio = RealtimeEncryptedAudio::decode(
      std::span<const uint8_t>(packet,static_cast<size_t>(count)),realtimeSessionKey(*conn));
  if (!audio) reportRealtimeAudioError(audio.error());
  else submitRealtimeAudio(*audio,*conn);
}

static double controlStreamSeconds(int32_t stream,unsigned rate) {
  const auto frames = static_cast<int64_t>(stream)+11025;
  if (rate != 0) return frames*1.0/rate;
  if (frames == 0) return std::numeric_limits<double>::quiet_NaN();
  return frames < 0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
}
void *rtp_ap2_control_receiver(void *arg) {
  pthread_cleanup_push(rtp_ap2_control_handler_cleanup_handler,arg);
  auto *conn = static_cast<rtsp_conn_info *>(arg);
  uint8_t packet[4096];
  ControlPacketCoordinator coordinator(get_absolute_time_in_ns());
  const std::function<double()> draw = drand48;
  while (true) {
    const auto received = conn->controlChannel.receive(packet);
    if (received.stopped) break;
    const auto count = received.count;
    const int error = received.errorCode;
    const auto now = get_absolute_time_in_ns();
    const ControlLiveSettings live{
        conn->airplay_stream_type == realtime_stream,conn->inputAudio.sampleRate(),
        config.audio_backend_buffer_desired_length,conn->timingSettings().offsetSeconds(),
        conn->latency_warning_issued != 0,config.diagnostic_drop_packet_fraction};
    const auto decision = coordinator.handle(
        {std::span<const uint8_t>(packet,count < 0 ? 0 : static_cast<size_t>(count)),error,now},live,draw);
    for (const auto &effect : decision) {
      switch (effect.kind) {
      case ControlEffectKind::initialized:
        debug(2,"AP2 Realtime Clock receiver initialised.");
        break;
      case ControlEffectKind::stale:
        debug(1,"Dropping what looks like a (non-sentinel) packet left over from a previous session at %f ms.",
              0.000001*effect.elapsedNs);
        break;
      case ControlEffectKind::firstSentinel:
        debug(2,effect.sentinel ? "First packet is a sentinel packet." : "First packet is a not a sentinel packet!");
        break;
      case ControlEffectKind::diagnosticDrop:
        debug(1,"AP2 Control Receiver -- dropping a packet.");
        break;
      case ControlEffectKind::capturePeerIfAbsent:
        conn->remoteControlPeer.captureIfAbsent(reinterpret_cast<const sockaddr *>(&received.peer.address),received.peer.length);
        break;
      case ControlEffectKind::unexpectedLatency:
        debug(1,"Stream-specified latency is %d frames. Normally it is 77175.",effect.stream);
        break;
      case ControlEffectKind::warnAndZeroSharedOffset:
        warn("The stream latency (%g seconds) is too short to accommodate an audio backend latency offset of "
             "%g seconds and a backend buffer of %g seconds. The audio_backend_latency_offset has been set to zero.",
             controlStreamSeconds(effect.stream,live.inputRate),
             live.sharedOffsetSeconds,live.backendBufferSeconds);
        conn->timingSettings().disableForInsufficientLatency();
        conn->latency_warning_issued = 1;
        break;
      case ControlEffectKind::setLatency:
        conn->latency = static_cast<uint32_t>(effect.latency);
        break;
      case ControlEffectKind::setAnchor:
        set_ptp_anchor_info(conn,effect.clockId,effect.anchorFrame,effect.timeNs);
        break;
      case ControlEffectKind::retransmit:
        decipher_player_put_packet(const_cast<uint8_t *>(effect.bytes.data()),effect.bytes.size(),conn);
        break;
      case ControlEffectKind::unknown: {
        char *hex = debug_malloc_hex_cstring(const_cast<uint8_t *>(effect.bytes.data()),effect.bytes.size());
        debug(1,"AP2 Control Receiver Packet of first byte 0x%02X, type 0x%02X length %zd received: \"%s\".",
              effect.bytes[0],effect.bytes[1],static_cast<ssize_t>(effect.bytes.size()),hex);
        free(hex);
        break;
      }
      case ControlEffectKind::resetAnchor:
        debug(1,"Connection %d: no control packets for the last 7 seconds -- resetting anchor info",conn->connection_number);
        reset_ptp_anchor_info(conn);
        break;
      case ControlEffectKind::error:
        debug(2,"Connection %d: AP2 Control Receiver -- error %d receiving a packet.",conn->connection_number,effect.errorCode);
        break;
      case ControlEffectKind::malformed:
        debug(2,"Connection %d: AP2 Control Receiver -- malformed packet, %zd bytes long.",
              conn->connection_number,static_cast<ssize_t>(effect.length));
        break;
      case ControlEffectKind::invalidLatency:
        debug(1,"Invalid AP2 control latency -- timing packet ignored.");
        break;
      }
    }
  }
  pthread_cleanup_pop(1);
  pthread_exit(nullptr);
}

void rtp_realtime_audio_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Realtime Audio Receiver Cleanup Start.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: closing realtime audio port %u", conn->connection_number,
        conn->local_realtime_audio_port);
  safe_socket_close(&conn->realtime_audio_socket);
  debug(2, "Realtime Audio Receiver Cleanup Done.");
}

class RealtimeSocketInput : public DatagramInput {
public:
  explicit RealtimeSocketInput(int socket) : socket_(socket) {}
  DatagramRead read(std::span<uint8_t> destination) override {
    const auto count = recv(socket_,destination.data(),destination.size(),0);
    if (count < 0) return {0,errno};
    return {static_cast<size_t>(count),0};
  }
private:
  int socket_;
};
class RealtimeRandomSource : public RandomSource {
public:
  double draw() override { return drand48(); }
};
void *rtp_realtime_audio_receiver(void *arg) {
  pthread_cleanup_push(rtp_realtime_audio_cleanup_handler,arg);
  auto *conn = static_cast<rtsp_conn_info *>(arg);
  RealtimeSocketInput input(conn->realtime_audio_socket);
  RealtimeRandomSource random;
  RealUDPIngress ingress(input,random,config.diagnostic_drop_packet_fraction);
  while (true) {
    auto outcome = ingress.one(realtimeSessionKey(*conn));
    switch (outcome.kind) {
    case RealtimeReceiveKind::readError:
    case RealtimeReceiveKind::shortPacket:
      debug(1,"Realtime Audio Receiver -- error receiving a packet.");
      break;
    case RealtimeReceiveKind::dropped:
      debug(3,"Realtime Audio Receiver -- dropping a packet.");
      break;
    case RealtimeReceiveKind::rejected:
      reportRealtimeAudioError(outcome.rejection);
      break;
    case RealtimeReceiveKind::audio:
      submitRealtimeAudio(*outcome.audio,*conn);
      break;
    }
  }
  pthread_cleanup_pop(0);
  pthread_exit(nullptr);
}

int frame_to_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  return frame_to_ptp_local_time(timestamp, time, conn);
}

int local_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  return local_ptp_time_to_frame(time, frame, conn);
}

void reset_anchor_info(rtsp_conn_info *conn) {
  reset_ptp_anchor_info(conn);
}

int have_timestamp_timing_information(rtsp_conn_info *conn) {
  return have_ptp_timing_information(conn);
}

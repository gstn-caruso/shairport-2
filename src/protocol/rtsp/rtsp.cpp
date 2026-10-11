/*
 * RTSP protocol handler. This file is part of Shairport Sync
 * Copyright (c) James Laird 2013

 * Modifications, including those associated with audio synchronization, multithreading and
 * metadata handling copyright (c) Mike Brady 2014--2026
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
#include "audio/output/audio_player_adapter.hpp"
#include "volume/volume_runtime.hpp"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <limits.h>
#include <memory.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "monitoring/activity_monitor.h"
#include "config.h"
#include "platform/utilities/network_utilities.h"
#include "platform/utilities/rtsp_message_utilities.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "protocol/rtsp/rtsp_listener.hpp"
#include "platform/utilities/exit.h"
#include <format>
#include <new>

import receiver.protocol.rtsp.parameters;

#include <openssl/evp.h>
#include <openssl/md5.h>



#include "discovery/bonjour_strings.h"
#include "runtime/common.h"
#include "playback/player.h"
#include "protocol/rtp/rtp.h"
#include "protocol/rtsp/rtsp.h"



#ifdef AF_INET6
#define INETx_ADDRSTRLEN INET6_ADDRSTRLEN
#else
#define INETx_ADDRSTRLEN INET_ADDRSTRLEN
#endif

#include "protocol/ap2/ap2_buffered_audio_processor.h"
#include "protocol/ap2/ap2_event_receiver.h"
#include "platform/utilities/pairing_api.h"
#include <plists/get_info_response.h>
#include "timing/ptp-utilities.h"
#include <plist/plist.h>

#ifdef HAVE_LIBPLIST_GE_2_3_0
#define plist_from_memory(plist_data, length, plist)                                               \
  plist_from_memory((plist_data), (length), (plist), NULL)
#endif



#include "discovery/mdns.h"
#include "platform/utilities/network_utilities.h"


#include "session/session_registry.hpp"
#include "session/runtime_session_worker.hpp"
#include "session/runtime_principal_session.hpp"
import receiver.protocol.rtsp.request;

static RuntimePrincipalSession principalSession;
static SessionRegistry sessions;
int RTSP_connection_index = 1;

static void publishPrincipalSession() {
  principalSession.withCurrent([](SessionState *current) {
    if (current)
      config.airplay_statusflags |= (1 << 11);
    else
      config.airplay_statusflags &= ~(1U << 11);
    build_bonjour_strings(current);
    mdns_update(nullptr, secondary_txt_records);
  });
}

void cancel_all_RTSP_threads(airplay_stream_c category, int exceptId) {
  sessions.cancelAndJoinMatching(category, exceptId);
}

void cleanup_threads() {
  sessions.joinFinished();
}

int terminate_conn(int id) {
  return sessions.cancelAndJoin(id);
}

void stop_play() {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  auto previous = principalSession.clear();
  if (previous) {
    publishPrincipalSession();
    terminate_conn(*previous);
  }
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
}

void release_play_lock(rtsp_conn_info *conn) {
  bool released = conn ? principalSession.releaseIfCurrent(conn->connection_number)
                       : principalSession.clear().has_value();
  if (released)
    publishPrincipalSession();
}

typedef enum {
  play_lock_released,
  play_lock_already_released,
  play_lock_already_acquired,
  play_lock_acquired_without_breaking_in,
  play_lock_acquired_by_breaking_in,
  play_lock_aquisition_failed
} play_lock_r;

play_lock_r get_play_lock(rtsp_conn_info *conn, int allow_session_interruption) {
  if (!conn)
    return play_lock_aquisition_failed;
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  auto acquisition = principalSession.acquire(*conn, allow_session_interruption != 0);
  auto response = play_lock_aquisition_failed;
  if (acquisition.accepted) {
    if (acquisition.alreadyCurrent)
      response = play_lock_already_acquired;
    else {
      publishPrincipalSession();
      if (acquisition.previousId)
        terminate_conn(*acquisition.previousId);
      response = acquisition.previousId ? play_lock_acquired_by_breaking_in
                                        : play_lock_acquired_without_breaking_in;
    }
  }
  pthread_setcancelstate(previousState, nullptr);
  pthread_testcancel();
  return response;
}

static void buf_add(sized_buffer *buf, uint8_t *in, size_t in_len) {
  if (buf->length + in_len > buf->size) {
    buf->size = buf->length + in_len + 2048; // Extra headroom to avoid future memcpy's
    buf->data = static_cast<uint8_t *>(realloc(buf->data, buf->size));
  }
  memcpy(buf->data + buf->length, in, in_len);
  buf->length += in_len;
}

static void buf_drain(sized_buffer *buf, ssize_t len) {
  if (len < 0 || (size_t)len >= buf->length) {
    free(buf->data);
    memset(buf, 0, sizeof(sized_buffer));
    return;
  }
  memmove(buf->data, buf->data + len, buf->length - len);
  buf->length -= len;
}

void pair_cipher_bundle::release() {
  buf_drain(&plaintext_read_buffer, -1);
  buf_drain(&encrypted_read_buffer, -1);
  if (description != nullptr)
    free(description);
  description = nullptr;
  auto *cipher = cipher_ctx;
  cipher_ctx = nullptr;
  pair_cipher_free(cipher);
}

static size_t buf_remove(sized_buffer *buf, uint8_t *out, size_t out_len) {
  size_t bytes = (buf->length > out_len) ? out_len : buf->length;
  memcpy(out, buf->data, bytes);
  buf_drain(buf, bytes);
  return bytes;
}

ssize_t read_encrypted(int fd, pair_cipher_bundle *ctx, void *buf, size_t count) {
  ssize_t response = 0;
  // If there is leftover decoded content from the last pass just return that
  if (ctx->plaintext_read_buffer.length > 0) {
    response = buf_remove(&ctx->plaintext_read_buffer, static_cast<uint8_t *>(buf), count);
  } else {

    // Otherwise read stuff in...
    uint8_t in[4096];
    uint8_t *plain = NULL; // may be allocated and reallocated by pair_decrypt
    pthread_cleanup_push(malloc_cleanup, &plain);
    size_t plain_len = 0;
    do {
      response = read(fd, in, sizeof(in));
      if (response > 0) {
        buf_add(&ctx->encrypted_read_buffer, in, response);
        ssize_t consumed = pair_decrypt(&plain, &plain_len, ctx->encrypted_read_buffer.data,
                                        ctx->encrypted_read_buffer.length, ctx->cipher_ctx);
        if (consumed < 0) {
          debug(1, "read_encrypted: abnormal exit from pair_decrypt: %zd.", consumed);
          response = -1;
        } else {
          buf_drain(&ctx->encrypted_read_buffer, consumed);
        }
      }
    } while ((plain_len == 0) && (response > 0));

    if (response >= 0) {
      buf_add(&ctx->plaintext_read_buffer, plain, plain_len);
      response = buf_remove(&ctx->plaintext_read_buffer, static_cast<uint8_t *>(buf), count);
    }
    pthread_cleanup_pop(1);
  }

  return response;
}

ssize_t write_encrypted(int fd, pair_cipher_bundle *ctx, const void *buf, size_t count) {
  uint8_t *encrypted;
  size_t encrypted_len;

  ssize_t ret = pair_encrypt(&encrypted, &encrypted_len, static_cast<const uint8_t *>(buf), count, ctx->cipher_ctx);
  if (ret < 0) {
    debug(1, "%s", pair_cipher_errmsg(ctx->cipher_ctx));
    return -1;
  }

  size_t remain = encrypted_len;
  // debug(1, "write to the \"%s\" channel", ctx->description);
  // debug_print_buffer(1, (void *)buf, count);
  // debug(1, "write encrypted:");
  // debug_print_buffer(1, encrypted, encrypted_len);
  while (remain > 0) {
    ssize_t wrote = write(fd, encrypted + (encrypted_len - remain), remain);
    if (wrote <= 0) {
      free(encrypted);
      return wrote;
    }
    remain -= wrote;
  }
  free(encrypted);
  return count;
}


ssize_t read_from_rtsp_connection(rtsp_conn_info *conn, void *buf, size_t count) {
  if (count == 0)
    debug(1, "asking to read zero bytes!");

  ssize_t result = 0; // closed
  if (conn->fd > 0) {
    if (conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx) {
      conn->ap2_pairing_context.control_cipher_bundle.is_encrypted = 1;
      result =
          read_encrypted(conn->fd, &conn->ap2_pairing_context.control_cipher_bundle, buf, count);

    } else {
      result = read(conn->fd, buf, count);
    }
    if ((result <= 0) && (errno != 0)) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(3, "read_from_rtsp_connection error %d \"%s\" attempting to read up to %zu bytes.",
            errno, errorstring, count);
    }
  } else {
    debug(1, "Connection %d: attempt to read from a closed RTSP connection.",
          conn->connection_number);
  }
  return result;
}

void set_client_as_ptp_clock(rtsp_conn_info *conn) {
  char timing_list_message[4096] = "";
  strncat(timing_list_message, "T ", sizeof(timing_list_message) - 1 - strlen(timing_list_message));
  strncat(timing_list_message, (const char *)&conn->client_ip_string,
          sizeof(timing_list_message) - 1 - strlen(timing_list_message));
  ptp_send_control_message_string(timing_list_message);
}

void msg_cleanup_function(void *arg);

namespace {
class RuntimeRequestTransport : public RtspRequestInput,
                                public RtspRequestClock,
                                public RtspRequestEffects {
public:
  explicit RuntimeRequestTransport(SessionState &session) : session_(session) {}
  bool stopped() override { return session_.stop != 0; }
  RtspRequestRead read(std::span<char> destination) override {
    const auto count = read_from_rtsp_connection(&session_, destination.data(), destination.size());
    const auto error = errno;
    return {count, error, error == ETIMEDOUT};
  }
  std::uint64_t nowNs() override { return get_absolute_time_in_ns(); }
  void closeHeaderChannel() override { safe_socket_close(&session_.fd); }
  void stalled() override {
    debug(1, "Error receiving metadata from source -- transmission seems to be stalled.");
  }
  void diagnostic(RtspRequestDiagnostic event, RtspRequestPhase phase, int error) override {
    const auto id = session_.connection_number;
    switch (event) {
    case RtspRequestDiagnostic::shutdown:
      if (phase == RtspRequestPhase::headers)
        debug(3, "Connection %d: shutdown requested by client.", id);
      else
        debug(1, "RTSP shutdown requested.");
      break;
    case RtspRequestDiagnostic::timeout:
      debug(1, "Connection %d has disappeared. As Yeats almost said, \"Too long a "
               "silence / can make a stone of the heart\". ETIMEOUT", id);
      break;
    case RtspRequestDiagnostic::closed: {
      const auto level = phase == RtspRequestPhase::headers ? 2 : 1;
      const char *channel = phase == RtspRequestPhase::headers ? " RTSP" : "";
      if (error == 0) {
        debug(level, "Connection %d%s closed by client.", id, channel);
      } else {
        channel = phase == RtspRequestPhase::headers ? " RTSP port" : "";
        char description[1024];
        strerror_r(error, description, sizeof(description));
        debug(level, "Connection %d%s closed by client with error %d: \"%s\".",
              id, channel, error, description);
      }
      break;
    }
    case RtspRequestDiagnostic::readError: {
      char description[1024];
      strerror_r(error, description, sizeof(description));
      debug(1, "Connection %d: rtsp_read_request_response_read_error %d: \"%s\".",
            id, error, description);
      break;
    }
    case RtspRequestDiagnostic::badPacket:
      debug(1, "Connection %d: rtsp_read_request can't find an RTSP header.", id);
      break;
    case RtspRequestDiagnostic::allocationFailure:
      if (phase == RtspRequestPhase::headers)
        debug(1, "Connection %d: rtsp_read_request: can't get a buffer.", id);
      else
        warn("Connection %d: too much content.", id);
      break;
    }
  }
private:
  SessionState &session_;
};
}

enum rtsp_read_request_response rtsp_read_request(rtsp_conn_info *conn, RtspMessage **the_packet) {
  *the_packet = nullptr;
  RuntimeRequestTransport transport(*conn);
  RtspRequestReader reader(transport, transport, transport);
  auto result = reader.read();
  switch (result.status) {
  case RtspRequestStatus::success:
    *the_packet = result.message.release();
    return rtsp_read_request_response_ok;
  case RtspRequestStatus::shutdown:
    return rtsp_read_request_response_immediate_shutdown_requested;
  case RtspRequestStatus::badPacket:
    return rtsp_read_request_response_bad_packet;
  case RtspRequestStatus::channelClosed:
    return rtsp_read_request_response_channel_closed;
  case RtspRequestStatus::readError:
    return rtsp_read_request_response_read_error;
  case RtspRequestStatus::allocationFailure:
    return rtsp_read_request_response_error;
  }
  std::terminate();
}

int msg_write_response(rtsp_conn_info *conn, RtspMessage *response) {
  auto packet = response->responsePacket();
  if (!packet)
    return static_cast<int>(packet.error());

  ssize_t written;
  if (conn->ap2_pairing_context.control_cipher_bundle.is_encrypted)
    written = write_encrypted(conn->fd, &conn->ap2_pairing_context.control_cipher_bundle,
                              packet->data(), packet->size());
  else
    written = write(conn->fd, packet->data(), packet->size());
  if (written < 0)
    return -4;
  if (static_cast<size_t>(written) != packet->size())
    return -5;
  return 0;
}

void handle_record_2(rtsp_conn_info *conn, __attribute((unused)) RtspMessage *req,
                     RtspMessage *resp) {
  debug(2, "Connection %d: RECORD (AP2) on %s", conn->connection_number,
        get_category_string(conn->airplay_stream_category));
  debug_log_rtsp_message_conn(conn, 2, "RECORD (AP2) incoming message", req);
  resp->addHeader("Audio-Latency", "0");
  resp->respondWith(200);
}



int add_pstring_to_malloc(const char *s, void **allocation, size_t *size) {
  int response = 0;
  void *p = *allocation;
  if (p == NULL) {
    p = malloc(strlen(s) + 1);
    if (p == NULL) {
      debug(1, "error allocating memory");
    } else {
      *allocation = p;
      *size = *size + strlen(s) + 1;
      uint8_t *b = (uint8_t *)p;
      *b = strlen(s);
      p = static_cast<char *>(p) + 1;
      memcpy(p, s, strlen(s));
      response = 1;
    }
  } else {
    p = realloc(p, *size + strlen(s) + 1);
    if (p == NULL) { // assuming we never allocate a zero byte space
      debug(1, "error reallocating memory");
    } else {
      *allocation = p;
      uint8_t *b = (uint8_t *)p + *size;
      *b = strlen(s);
      p = static_cast<char *>(p) + *size + 1;
      memcpy(p, s, strlen(s));
      *size = *size + strlen(s) + 1;
      response = 1;
    }
  }
  return response;
}

void generateTxtDataValueInfo(rtsp_conn_info *conn, void **response, size_t *responseLength) {
  void *qualifier_response_data = NULL;
  size_t qualifier_response_data_length = 0;
  char localString[256];
  if (add_pstring_to_malloc("acl=0", &qualifier_response_data, &qualifier_response_data_length) ==
      0)
    debug(1, "Problem");
  if (add_pstring_to_malloc("btaddr=00:00:00:00:00:00", &qualifier_response_data,
                            &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "deviceid=%s", config.airplay_device_id),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "fex=%s", config.airplay_fex),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  /*
    uint64_t features_hi = 0x0001C340445D0A00L;
    features_hi = (features_hi >> 32) & 0xffffffff;
    uint64_t features_lo = 0x0001C340445D0A00L;
    features_lo = features_lo & 0xffffffff;
  */

  uint64_t features_hi = config.airplay_features;
  features_hi = (features_hi >> 32) & 0xffffffff;
  uint64_t features_lo = config.airplay_features;
  features_lo = features_lo & 0xffffffff;

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString),
                                     "features=0x%" PRIX64 ",0x%" PRIX64 "", features_lo,
                                     features_hi),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  // if (add_pstring_to_malloc("rsf=0x0", &qualifier_response_data,
  //                             &qualifier_response_data_length) == 0)
  //   debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "flags=0x%x", config.airplay_statusflags),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if ((conn != NULL) && (conn->airplay_gid != 0)) {
    snprintf(localString, sizeof(localString), "gid=%s", conn->airplay_gid);
  } else {
    snprintf(localString, sizeof(localString), "gid=%s", config.airplay_pi);
  }

  if (add_pstring_to_malloc(localString, &qualifier_response_data,
                            &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  int gcgl = 0;
  if (conn != NULL)
    gcgl = conn->groupContainsGroupLeader;

  // should have igl here;
  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "igl=%d", 0),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "gcgl=%d", gcgl),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "pgid=%s", config.airplay_pgid),
          &qualifier_response_data, &qualifier_response_data_length) == 0)

    if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "pgcgl=%d", gcgl),
                              &qualifier_response_data, &qualifier_response_data_length) == 0)
      debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "model=%s", config.model),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  if (add_pstring_to_malloc("protovers=1.1", &qualifier_response_data,
                            &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "pi=%s", config.airplay_pi),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "psi=%s", config.airplay_psi),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "pk=%s", config.pk_string),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "srcvers=%s", config.srcvers),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "osvers=%s", config.osvers),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc("vv=2", &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  *response = qualifier_response_data;
  *responseLength = qualifier_response_data_length;
}

plist_t generateInfoPlist(rtsp_conn_info *conn) {
  plist_t response_plist = NULL;

  plist_from_memory((const char *)get_info_response_plist, get_info_response_plist_len,
                    &response_plist);

  if (response_plist == NULL) {
    debug(1, "generateInfoPlist plist not created!");
  } else {
    const auto statusFlags = principalSession.withCurrent([](SessionState *) {
      return config.airplay_statusflags;
    });

    // debug(1,"qualifier_response_data_length: %u.", qualifier_response_data_length);

    plist_dict_set_item(response_plist, "psi", plist_new_string(config.airplay_psi));

    plist_dict_set_item(response_plist, "featuresEx", plist_new_string(config.airplay_fex));

    plist_dict_set_item(response_plist, "features", plist_new_uint(config.airplay_features));
    plist_dict_set_item(response_plist, "statusFlags", plist_new_uint(statusFlags));
    plist_dict_set_item(response_plist, "deviceID", plist_new_string(config.airplay_device_id));
    plist_dict_set_item(response_plist, "pi", plist_new_string(config.airplay_pi));
    plist_dict_set_item(response_plist, "name", plist_new_string(config.service_name));
    plist_dict_set_item(response_plist, "model", plist_new_string(config.model));
    plist_dict_set_item(response_plist, "pk",
                        plist_new_data((const char *)config.airplay_pk, sizeof(config.airplay_pk)));
    char senderAddress[256];
    snprintf(senderAddress, sizeof(senderAddress), "%s:%u", conn->client_ip_string,
             conn->client_rtsp_port);
    plist_dict_set_item(response_plist, "senderAddress", plist_new_string(senderAddress));
    plist_dict_set_item(response_plist, "initialVolume", plist_new_real(suggestedSessionVolume(conn).value()));
    plist_dict_set_item(response_plist, "sourceVersion", plist_new_string(config.srcvers));

    // Create a dictionary of supported formats for the bufferStream
    uint64_t bufferStreamFormats = 0L;
    // bufferStreamFormats = 0xF7FE000E00000000; // don't know what these do (from the HPm)
    plist_t supported_formats_plist = plist_new_dict();
    if (supported_formats_plist != NULL) {
      plist_dict_set_item(supported_formats_plist, "audioStream", plist_new_uint(21235712));
      {
        bufferStreamFormats |= 0x00000400000L; // AAC-LC/44.1K/F24/2
        bufferStreamFormats |= 0x40000;        // ALAC/44100/S16/2
      }

      {
        bufferStreamFormats |= 0x00000200000L; // ALAC/48K/F24/2
        bufferStreamFormats |= 0x00000800000L; // AAC-LC/48K/F24/2
      }
      {
        if (config.eight_channel_layout != 0)
          bufferStreamFormats |= 0x10000000000L; // AAC-LC/48K/F24/7.1
        if (config.six_channel_layout != 0)
          bufferStreamFormats |= 0x08000000000L; // AAC-LC/48K/F24/5.1
      }
      plist_dict_set_item(supported_formats_plist, "bufferStream",
                          plist_new_uint(bufferStreamFormats));
      debug(4, "bufferedStream formats: 0x%" PRIX64 ".", bufferStreamFormats);
      plist_dict_set_item(response_plist, "supportedFormats", supported_formats_plist);
    }
  }
  return response_plist;
}

void handle_get_info(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug_log_rtsp_message(3, "GET /info:", req);
  if (req->bodyStartsWith("bplist00")) { // it's stage one
    // get version of AirPlay -- it might be too old. Not using it yet.
    const char *hdr = req->headerValue("User-Agent");
    if (hdr) {
      if (strstr(hdr, "AirPlay/") == hdr) {
        hdr = hdr + strlen("AirPlay/");
        // double airplay_version = 0.0;
        // airplay_version = atof(hdr);
        debug(3, "Connection %d: GET_INFO: Source AirPlay Version is: %s.", conn->connection_number,
              hdr);
      }
    }

    // in Stage 1, look for the DACP and Active-Remote
    const char *ar = req->headerValue("Active-Remote");
    if (ar) {
      debug(3, "Connection %d: GET /info -- Active-Remote string seen: \"%s\".",
            conn->connection_number, ar);
      // get the active remote
      if (conn->dacp_active_remote) // this is in case SETUP was previously called
        free(conn->dacp_active_remote);
      conn->dacp_active_remote = strdup(ar);
    } else {
      debug(3, "Connection %d: GET /info -- doesn't include  Active-Remote information.",
            conn->connection_number);
      if (conn->dacp_active_remote) { // this is in case GET /info was previously called
        free(conn->dacp_active_remote);
        conn->dacp_active_remote = NULL;
      }
    }

    ar = req->headerValue("DACP-ID");
    if (ar) {
      debug(3, "Connection %d: GET /info -- DACP-ID string seen: \"%s\".", conn->connection_number,
            ar);
      if (conn->dacp_id) // this is in case SETUP was previously called
        free(conn->dacp_id);
      conn->dacp_id = strdup(ar);
    } else {
      debug(3, "Connection %d: GET /info -- doesn't include DACP-ID string information.",
            conn->connection_number);
      if (conn->dacp_id) { // this is in case GET /info was previously called
        free(conn->dacp_id);
        conn->dacp_id = NULL;
      }
    }

    plist_t info_plist = NULL;
    plist_from_memory(req->bodyData(), req->bodyLength(), &info_plist);

    plist_t qualifier = plist_dict_get_item(info_plist, "qualifier");
    if (qualifier == NULL) {
      debug(1, "GET /info Stage 1: plist->qualifier was NULL");
      resp->respondWith(400);
      return;
    }
    if (plist_array_get_size(qualifier) < 1) {
      debug(1, "GET /info Stage 1: plist->qualifier array length < 1");
      resp->respondWith(400);
      return;
    }
    plist_t qualifier_array_value = plist_array_get_item(qualifier, 0);
    char *qualifier_array_val_cstr;
    plist_get_string_val(qualifier_array_value, &qualifier_array_val_cstr);
    if (qualifier_array_val_cstr == NULL) {
      debug(1, "GET /info Stage 1: first item in qualifier array not a string");
      resp->respondWith(400);
      return;
    }
    debug(3, "GET /info Stage 1: qualifier: %s", qualifier_array_val_cstr);
    plist_free(info_plist);
    free(qualifier_array_val_cstr);

    plist_t response_plist = generateInfoPlist(conn);

    if (response_plist == NULL) {
      resp->respondWith(400);
      return;
    }

    void *txtData = NULL;
    size_t txtDataLength = 0;
    generateTxtDataValueInfo(conn, &txtData, &txtDataLength);
    plist_dict_set_item(response_plist, "txtAirPlay", plist_new_data(static_cast<const char *>(txtData), txtDataLength));
    free(txtData);
    replaceBodyWithPlist(*resp, response_plist);
    if (resp->bodyLength() == 0)
      debug(1, "GET /info Stage 1: response bplist not created!");
    plist_free(response_plist);
    /*
        free(qualifier_response_data);
    */

    resp->addHeader("Content-Type", "application/x-apple-binary-plist");
    resp->respondWith(200);
    debug_log_rtsp_message(3, "GET /info Stage 1 Response:", resp);
    return;

  } else { // stage two
    plist_t response_plist = generateInfoPlist(conn);

    if (response_plist == NULL) {
      resp->respondWith(400);
      return;
    }

    void *txtData = NULL;
    size_t txtDataLength = 0;
    generateTxtDataValueInfo(conn, &txtData, &txtDataLength);
    plist_dict_set_item(response_plist, "txtAirPlay", plist_new_data(static_cast<const char *>(txtData), txtDataLength));
    free(txtData);
    replaceBodyWithPlist(*resp, response_plist);
    plist_free(response_plist);
    resp->addHeader("Content-Type", "application/x-apple-binary-plist");
    resp->respondWith(200);
    debug_log_rtsp_message(3, "GET /info Stage 2 Response", resp);
    return;
  }
}

void handle_flushbuffered(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(2, "Connection %d: FLUSHBUFFERED %s : Content-Length %d", conn->connection_number,
        req->requestPath(), req->bodyLength());
  debug_log_rtsp_message(3, "FLUSHBUFFERED request", req);

  uint64_t flushFromSeq = 0;
  uint64_t flushFromTS = 0;
  uint64_t flushUntilSeq = 0;
  uint64_t flushUntilTS = 0;
  int flushFromValid = 0;
  plist_t messagePlist = plistFromMessageBody(*req);
  if (messagePlist != NULL) {
    plist_t item = plist_dict_get_item(messagePlist, "flushFromSeq");
    if (item == NULL) {
      debug(4, "Can't find a flushFromSeq");
    } else {
      flushFromValid = 1;
      plist_get_uint_val(item, &flushFromSeq);
      debug(3, "flushFromSeq is %" PRId64 ".", flushFromSeq & 0x7fffff);
    }

    item = plist_dict_get_item(messagePlist, "flushFromTS");
    if (item == NULL) {
      if (flushFromValid != 0)
        debug(1, "flushFromSeq without flushFromTS!");
      else
        debug(4, "Can't find a flushFromTS");
    } else {
      plist_get_uint_val(item, &flushFromTS);
      if (flushFromValid == 0)
        debug(1, "flushFromTS without flushFromSeq!");
      debug(3, "flushFromTS is %" PRId64 ".", flushFromTS);
    }

    item = plist_dict_get_item(messagePlist, "flushUntilSeq");
    if (item == NULL) {
      debug(1, "Can't find the flushUntilSeq");
    } else {
      plist_get_uint_val(item, &flushUntilSeq);
      debug(4, "flushUntilSeq is %" PRId64 ".", flushUntilSeq & 0x7fffff);
    }

    item = plist_dict_get_item(messagePlist, "flushUntilTS");
    if (item == NULL) {
      debug(1, "Can't find the flushUntilTS");
    } else {
      plist_get_uint_val(item, &flushUntilTS);
      debug(4, "flushUntilTS is %" PRId64 ".", flushUntilTS);
    }

    pthread_mutex_lock(&conn->flush_mutex);

    if (flushFromValid == 0) {
      // an immediate flush is requested
      conn->bufferedFlush.requestImmediate(static_cast<uint32_t>(flushUntilSeq),
                                            static_cast<uint32_t>(flushUntilTS));
      debug(2,
            "Connection %d: immediate flush request created: flushUntilTS: %" PRIu64
            ", flushUntilSeq: %" PRIu64 ".",
            conn->connection_number, flushUntilTS, flushUntilSeq & 0x7fffff);
      conn->ap2_play_enabled = 0; // stop trying to play audio
      // ptp_send_control_message_string(
      //     "P"); // "P"ause signify clock no longer valid and will be restarted by a subsequent
      //     play
      // debug(1, "FLUSHBUFFERED calling reset_ptp_anchor_info");
      reset_ptp_anchor_info(
          conn); // stop the clock for an immediate flush until it is restarted using SETRATEANCHORI
    } else {
      if (conn->bufferedFlush.requestDeferred(static_cast<uint32_t>(flushFromSeq),
                                               static_cast<uint32_t>(flushFromTS),
                                               static_cast<uint32_t>(flushUntilSeq),
                                               static_cast<uint32_t>(flushUntilTS))) {
        debug(2,
              "Connection %d: deferred flush request created: flushFromSeq: %" PRIu64
              ", flushUntilSeq: %" PRIu64 ".",
              conn->connection_number, flushFromSeq, flushUntilSeq);
      } else {
        debug(1, "Connection %d: no more room for deferred flush request records",
              conn->connection_number);
      }
    }

    pthread_mutex_unlock(&conn->flush_mutex);
    plist_free(messagePlist);
  }

  resp->respondWith(200);
}

void handle_setrate(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(1, "Connection %d: SETRATE %s : Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(1, "SETRATE request -- unimplemented", req);
  resp->respondWith(501); // Not Implemented
}


void handle_setrateanchori(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(2, "Connection %d: SETRATEANCHORI %s :: Content-Length %d", conn->connection_number,
        req->requestPath(), req->bodyLength());
  debug_log_rtsp_message(3, "SETRATEANCHORI", req);
  plist_t messagePlist = plistFromMessageBody(*req);

  if (messagePlist != NULL) {
    pthread_cleanup_push(plist_cleanup, (void *)messagePlist);
    plist_t item = plist_dict_get_item(messagePlist, "networkTimeSecs");
    if (item != NULL) {
      plist_t item_2 = plist_dict_get_item(messagePlist, "networkTimeTimelineID");
      if (item_2 == NULL) {
        debug(1, "Can't identify the Clock ID of the player.");
      } else {
        uint64_t nid;
        plist_get_uint_val(item_2, &nid);
        debug(3, "networkTimeTimelineID \"%" PRIx64 "\".", nid);
        conn->networkTimeTimelineID = nid;
      }
      uint64_t networkTimeSecs;
      plist_get_uint_val(item, &networkTimeSecs);
      debug(3, "anchor networkTimeSecs is %" PRIu64 ".", networkTimeSecs);

      item = plist_dict_get_item(messagePlist, "networkTimeFrac");
      uint64_t networkTimeFrac;
      plist_get_uint_val(item, &networkTimeFrac);
      debug(3, "anchor networkTimeFrac is 0%" PRIu64 ".", networkTimeFrac);
      // it looks like the networkTimeFrac is a fraction where the msb is work 1/2, the
      // next 1/4 and so on
      // now, convert the network time and fraction into nanoseconds
      networkTimeFrac = networkTimeFrac >> 32; // reduce precision to about 1/4 nanosecond
      networkTimeFrac = networkTimeFrac * 1000000000;
      networkTimeFrac = networkTimeFrac >> 32; // we should now be left with the ns

      networkTimeSecs = networkTimeSecs * 1000000000; // turn the whole seconds into ns
      uint64_t anchorTimeNanoseconds = networkTimeSecs + networkTimeFrac;

      debug(3, "anchorTimeNanoseconds looks like %" PRIu64 ".", anchorTimeNanoseconds);

      item = plist_dict_get_item(messagePlist, "rtpTime");
      uint64_t rtpTime;

      plist_get_uint_val(item, &rtpTime);
      // debug(1, "anchor rtpTime is %" PRId64 ".", rtpTime);
      uint32_t anchorRTPTime = rtpTime;

      // Store the raw anchor; apply the latency offset when it is used.
      set_ptp_anchor_info(conn, conn->networkTimeTimelineID, anchorRTPTime, anchorTimeNanoseconds);
    }

    item = plist_dict_get_item(messagePlist, "rate");
    if (item != NULL) {
      uint64_t rate;
      plist_get_uint_val(item, &rate);
      debug(3, "anchor rate 0x%016" PRIx64 ".", rate);
      pthread_mutex_lock_and_cleanup_push(&conn->flush_mutex);
      conn->ap2_rate = rate;
      if ((rate & 1) != 0) {
        ptp_send_control_message_string(
            "B"); // signify clock dependability period is "B"eginning (or resuming)
        debug(2, "Connection %d: SETRATEANCHORI Start playing, with anchor clock %" PRIx64 ".",
              conn->connection_number, conn->networkTimeTimelineID);
        activity_monitor_signify_activity(1);

        conn->ap2_play_enabled = 1;
      } else {
        reset_ptp_anchor_info(conn);
        ptp_send_control_message_string("P"); // signify play is "P"ausing
        debug(2, "Connection %d: SETRATEANCHORI Pause playing.", conn->connection_number);
        conn->ap2_play_enabled = 0;
        activity_monitor_signify_activity(0);

        // if (config.output->stop) {
        //   debug(1, "Connection %d: SETRATEANCHORI would stop the output backend.",
        //   conn->connection_number); config.output->stop();
        // }
      }
      pthread_cleanup_pop(1); // unlock the conn->flush_mutex
    }
    pthread_cleanup_pop(1); // plist_free the messagePlist;
  } else {
    debug(1, "missing plist!");
  }
  resp->respondWith(200);
}

void handle_get(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(3, "Connection %d: GET %s :: Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(3, "GET request", req);
  if (req->requestsPath("/info")) {
    handle_get_info(conn, req, resp);
  } else {
    debug(1, "Unhandled GET, path \"%s\".", req->requestPath());
    resp->respondWith(501); // Not Implemented
  }
}


struct pairings {
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX];
  uint8_t public_key[32];

  struct pairings *next;
} *pairings;

static struct pairings *pairing_find(const char *device_id) {
  for (struct pairings *pairing = pairings; pairing; pairing = pairing->next) {
    if (strcmp(device_id, pairing->device_id) == 0)
      return pairing;
  }
  return NULL;
}

static void pairing_add(uint8_t public_key[32], const char *device_id) {
  struct pairings *pairing = static_cast<struct pairings *>(calloc(1, sizeof(struct pairings)));
  snprintf(pairing->device_id, sizeof(pairing->device_id), "%s", device_id);
  memcpy(pairing->public_key, public_key, sizeof(pairing->public_key));

  pairing->next = pairings;
  pairings = pairing;
}

static void pairing_remove(struct pairings *pairing) {
  if (pairing == pairings) {
    pairings = pairing->next;
  } else {
    struct pairings *iter;
    for (iter = pairings; iter && (iter->next != pairing); iter = iter->next)
      ; /* EMPTY */

    if (iter)
      iter->next = pairing->next;
  }

  free(pairing);
}

static int pairing_add_cb(uint8_t public_key[32], const char *device_id,
                          void *cb_arg __attribute__((unused))) {
  debug(1, "pair-add cb for %s", device_id);

  struct pairings *pairing = pairing_find(device_id);
  if (pairing) {
    memcpy(pairing->public_key, public_key, sizeof(pairing->public_key));
    return 0;
  }

  pairing_add(public_key, device_id);
  return 0;
}

static int pairing_remove_cb(uint8_t public_key[32] __attribute__((unused)), const char *device_id,
                             void *cb_arg __attribute__((unused))) {
  debug(1, "pair-remove cb for %s", device_id);

  struct pairings *pairing = pairing_find(device_id);
  if (!pairing) {
    debug(1, "pair-remove callback for device \"%s\".", device_id);
    return -1;
  }

  pairing_remove(pairing);
  return 0;
}

static void pairing_list_cb(pair_cb enum_cb, void *enum_cb_arg,
                            void *cb_arg __attribute__((unused))) {
  debug(1, "pair-list cb");

  for (struct pairings *pairing = pairings; pairing; pairing = pairing->next) {
    enum_cb(pairing->public_key, pairing->device_id, enum_cb_arg);
  }
}

void handle_pair_add(rtsp_conn_info *conn __attribute__((unused)), RtspMessage *req,
                     RtspMessage *resp) {

  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  debug(1, "Connection %d from \"%s\": handle_pair_add", conn->connection_number,
        conn->ap2_client_name);
  debug_log_rtsp_message_conn(conn, 1, "pair-add request", req);
  uint8_t *body = NULL;
  size_t body_len = 0;
  int ret = pair_add(PAIR_SERVER_HOMEKIT, &body, &body_len, pairing_add_cb, NULL,
                     (const uint8_t *)req->bodyData(), req->bodyLength());
  if (ret < 0) {
    debug(1, "pair-add returned an error");
    resp->respondWith(451);
    return;
  }
  replaceBodyFromAllocation(*resp, reinterpret_cast<char *>(body), body_len);
  resp->addHeader("Content-Type", "application/octet-stream");
  debug_log_rtsp_message_conn(conn, 1, "pair-add response", resp);
}

void handle_pair_list(rtsp_conn_info *conn __attribute__((unused)), RtspMessage *req,
                      RtspMessage *resp) {
  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  debug(1, "Connection %d from \"%s\": handle_pair_list", conn->connection_number,
        conn->ap2_client_name);
  uint8_t *body = NULL;
  size_t body_len = 0;
  int ret = pair_list(PAIR_SERVER_HOMEKIT, &body, &body_len, pairing_list_cb, NULL,
                      (const uint8_t *)req->bodyData(), req->bodyLength());
  if (ret < 0) {
    debug(1, "pair-list returned an error");
    resp->respondWith(451);
    return;
  }
  replaceBodyFromAllocation(*resp, reinterpret_cast<char *>(body), body_len);
  resp->addHeader("Content-Type", "application/octet-stream");
  debug_log_rtsp_message_conn(conn, 1, "pair-list response", resp);
}

void handle_pair_remove(rtsp_conn_info *conn __attribute__((unused)), RtspMessage *req,
                        RtspMessage *resp) {

  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  debug(1, "Connection %d from \"%s\": handle_pair_remove", conn->connection_number,
        conn->ap2_client_name);
  uint8_t *body = NULL;
  size_t body_len = 0;
  int ret = pair_remove(PAIR_SERVER_HOMEKIT, &body, &body_len, pairing_remove_cb, NULL,
                        (const uint8_t *)req->bodyData(), req->bodyLength());
  if (ret < 0) {
    debug(1, "pair-remove returned an error");
    resp->respondWith(451);
    return;
  }
  replaceBodyFromAllocation(*resp, reinterpret_cast<char *>(body), body_len);
  resp->addHeader("Content-Type", "application/octet-stream");
  debug_log_rtsp_message_conn(conn, 1, "pair-remove response", resp);
}

void handle_pair_verify(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  // try to pick up the stages
  const uint8_t *b = reinterpret_cast<const uint8_t *>(req->bodyData());
  char mstage = '-';
  if ((req->bodyLength() >= 3) && (b[0] == 6) && (b[1] == 1) && (b[1] <= 9)) {
    mstage = '0' + b[2];
  }

  debug(1, "Connection %d from \"%s\": handle_pair_verify, stage M%c, Content-Length %d",
        conn->connection_number, conn->ap2_client_name, mstage, req->bodyLength());
  debug_log_rtsp_message_conn(conn, 2, "pair-verify request", req);
  int ret;
  uint8_t *body = NULL;
  size_t body_len = 0;
  // struct pair_result *result;

  if (!conn->ap2_pairing_context.verify_ctx) {
    conn->ap2_pairing_context.verify_ctx =
        pair_verify_new(PAIR_SERVER_HOMEKIT, NULL, NULL, NULL, config.airplay_device_id);
    if (!conn->ap2_pairing_context.verify_ctx) {
      debug(1, "Error creating verify context");
      resp->respondWith(500); // Internal Server Error
      goto out;
    }
  }

  ret = pair_verify(&body, &body_len, conn->ap2_pairing_context.verify_ctx,
                    (const uint8_t *)req->bodyData(), req->bodyLength());
  if (ret < 0) {
    debug(1, "%s", pair_verify_errmsg(conn->ap2_pairing_context.verify_ctx));
    resp->respondWith(470); // Connection Authorization Required
    goto out;
  }

  /*
    ret = pair_verify_result(&result, conn->ap2_pairing_context.verify_ctx);
    if (ret == 0 && result->shared_secret_len > 0) {
      conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx =
          pair_cipher_new(PAIR_SERVER_HOMEKIT, 3, result->shared_secret, result->shared_secret_len);
      if (!conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx) {
        debug(1, "Error setting up rtsp control channel ciphering\n");
        goto out;
      }
      conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx =
          pair_cipher_new(PAIR_SERVER_HOMEKIT, 4, result->shared_secret, result->shared_secret_len);
      if (!conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx) {
        debug(1, "Error setting up rtsp event channel ciphering\n");
        goto out;
      }
    }
  */

out:
  replaceBodyFromAllocation(*resp, reinterpret_cast<char *>(body), body_len);
  if (body)
    resp->addHeader("Content-Type", "application/octet-stream");
  debug_log_rtsp_message_conn(conn, 2, "pair-verify response", resp);
}

void handle_pair_pin_start(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {

  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  debug(4, "Connection %d from \"%s\": handle_pair_pin_start, Content-Length %d",
        conn->connection_number, conn->ap2_client_name, req->bodyLength());
  debug_log_rtsp_message_conn(conn, 4, "handle_pair_pin_start", req);

  uint8_t *body = NULL;
  size_t body_len = 0;

  replaceBodyFromAllocation(*resp, reinterpret_cast<char *>(body), body_len);
  if (body != NULL)
    resp->addHeader("Content-Type", "application/octet-stream");
  debug_log_rtsp_message(4, "pair-pin-start response", resp);
}

void handle_pair_setup(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {

  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  debug(2, "Connection %d from \"%s\": handle_pair_setup, Content-Length %d",
        conn->connection_number, conn->ap2_client_name, req->bodyLength());
  debug_log_rtsp_message_conn(conn, 2, "pair-setup request", req);

  int ret;
  uint8_t *body = NULL;
  size_t body_len = 0;
  debug(3, "Connection %d: handle_pair_setup Content-Length %d", conn->connection_number,
        req->bodyLength());
  debug_log_rtsp_message(3, "handle_pair_setup", req);

  if (!conn->ap2_pairing_context.setup_ctx) {
    conn->ap2_pairing_context.setup_ctx =
        pair_setup_new(PAIR_SERVER_HOMEKIT, config.password, NULL, NULL, config.airplay_device_id);
    if (!conn->ap2_pairing_context.setup_ctx) {
      debug(1, "Error creating setup context");
      resp->respondWith(500); // Internal Server Error
      goto out;
    }
  }

  ret = pair_setup(&body, &body_len, conn->ap2_pairing_context.setup_ctx,
                   (const uint8_t *)req->bodyData(), req->bodyLength());
  if (ret < 0) {
    debug(1, "%s", pair_setup_errmsg(conn->ap2_pairing_context.setup_ctx));
    resp->respondWith(470); // Connection Authorization Required
    goto out;
  }

  ret = pair_setup_result(NULL, &conn->pair_setup_result, conn->ap2_pairing_context.setup_ctx);
  if (ret == 0 && conn->pair_setup_result->shared_secret_len > 0) {
    // Transient pairing completed (pair-setup step 2), prepare encryption, but
    // don't activate yet, the response to this request is still plaintext
    conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx =
        pair_cipher_new(PAIR_SERVER_HOMEKIT, 3, conn->pair_setup_result->shared_secret,
                        conn->pair_setup_result->shared_secret_len,
                        ""); // last argument is the (possible) dynamic salt suffix
    if (!conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx) {
      debug(1, "Error setting up rtsp control channel ciphering");
      goto out;
    }
    conn->ap2_pairing_context.control_cipher_bundle.description = strdup("Control Stream");

    conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx =
        pair_cipher_new(PAIR_SERVER_HOMEKIT, 4, conn->pair_setup_result->shared_secret,
                        conn->pair_setup_result->shared_secret_len,
                        ""); // last argument is the (possible) dynamic salt suffix
    if (!conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx) {
      debug(1, "Error setting up rtsp event channel ciphering");
      goto out;
    }
    conn->ap2_pairing_context.event_cipher_bundle.description = strdup("Event Stream");
  }

out:
  replaceBodyFromAllocation(*resp, reinterpret_cast<char *>(body), body_len);
  if (body)
    resp->addHeader("Content-Type", "application/octet-stream");
  debug_log_rtsp_message_conn(conn, 2, "pair-setup response", resp);
}

void handle_fp_setup(__attribute__((unused)) rtsp_conn_info *conn, RtspMessage *req,
                     RtspMessage *resp) {

  const char *hdr = req->headerValue("X-Apple-Client-Name");
  if (hdr) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    conn->ap2_client_name = strdup(hdr);
  }
  debug(2, "Connection %d from \"%s\": handle_fp_setup,", conn->connection_number,
        conn->ap2_client_name);
  debug_log_rtsp_message_conn(conn, 2, "fp-setup request", req);

  /* Fairplay magic */
  static uint8_t server_fp_reply1[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x00\x0f\x9f\x3f\x9e\x0a"
      "\x25\x21\xdb\xdf\x31\x2a\xb2\xbf\xb2\x9e\x8d\x23\x2b\x63\x76\xa8\xc8\x18\x70"
      "\x1d\x22\xae\x93\xd8\x27\x37\xfe\xaf\x9d\xb4\xfd\xf4\x1c\x2d\xba\x9d\x1f\x49"
      "\xca\xaa\xbf\x65\x91\xac\x1f\x7b\xc6\xf7\xe0\x66\x3d\x21\xaf\xe0\x15\x65\x95"
      "\x3e\xab\x81\xf4\x18\xce\xed\x09\x5a\xdb\x7c\x3d\x0e\x25\x49\x09\xa7\x98\x31"
      "\xd4\x9c\x39\x82\x97\x34\x34\xfa\xcb\x42\xc6\x3a\x1c\xd9\x11\xa6\xfe\x94\x1a"
      "\x8a\x6d\x4a\x74\x3b\x46\xc3\xa7\x64\x9e\x44\xc7\x89\x55\xe4\x9d\x81\x55\x00"
      "\x95\x49\xc4\xe2\xf7\xa3\xf6\xd5\xba";
  static uint8_t server_fp_reply2[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x01\xcf\x32\xa2\x57\x14"
      "\xb2\x52\x4f\x8a\xa0\xad\x7a\xf1\x64\xe3\x7b\xcf\x44\x24\xe2\x00\x04\x7e\xfc"
      "\x0a\xd6\x7a\xfc\xd9\x5d\xed\x1c\x27\x30\xbb\x59\x1b\x96\x2e\xd6\x3a\x9c\x4d"
      "\xed\x88\xba\x8f\xc7\x8d\xe6\x4d\x91\xcc\xfd\x5c\x7b\x56\xda\x88\xe3\x1f\x5c"
      "\xce\xaf\xc7\x43\x19\x95\xa0\x16\x65\xa5\x4e\x19\x39\xd2\x5b\x94\xdb\x64\xb9"
      "\xe4\x5d\x8d\x06\x3e\x1e\x6a\xf0\x7e\x96\x56\x16\x2b\x0e\xfa\x40\x42\x75\xea"
      "\x5a\x44\xd9\x59\x1c\x72\x56\xb9\xfb\xe6\x51\x38\x98\xb8\x02\x27\x72\x19\x88"
      "\x57\x16\x50\x94\x2a\xd9\x46\x68\x8a";
  static uint8_t server_fp_reply3[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x02\xc1\x69\xa3\x52\xee"
      "\xed\x35\xb1\x8c\xdd\x9c\x58\xd6\x4f\x16\xc1\x51\x9a\x89\xeb\x53\x17\xbd\x0d"
      "\x43\x36\xcd\x68\xf6\x38\xff\x9d\x01\x6a\x5b\x52\xb7\xfa\x92\x16\xb2\xb6\x54"
      "\x82\xc7\x84\x44\x11\x81\x21\xa2\xc7\xfe\xd8\x3d\xb7\x11\x9e\x91\x82\xaa\xd7"
      "\xd1\x8c\x70\x63\xe2\xa4\x57\x55\x59\x10\xaf\x9e\x0e\xfc\x76\x34\x7d\x16\x40"
      "\x43\x80\x7f\x58\x1e\xe4\xfb\xe4\x2c\xa9\xde\xdc\x1b\x5e\xb2\xa3\xaa\x3d\x2e"
      "\xcd\x59\xe7\xee\xe7\x0b\x36\x29\xf2\x2a\xfd\x16\x1d\x87\x73\x53\xdd\xb9\x9a"
      "\xdc\x8e\x07\x00\x6e\x56\xf8\x50\xce";
  static uint8_t server_fp_reply4[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x03\x90\x01\xe1\x72\x7e"
      "\x0f\x57\xf9\xf5\x88\x0d\xb1\x04\xa6\x25\x7a\x23\xf5\xcf\xff\x1a\xbb\xe1\xe9"
      "\x30\x45\x25\x1a\xfb\x97\xeb\x9f\xc0\x01\x1e\xbe\x0f\x3a\x81\xdf\x5b\x69\x1d"
      "\x76\xac\xb2\xf7\xa5\xc7\x08\xe3\xd3\x28\xf5\x6b\xb3\x9d\xbd\xe5\xf2\x9c\x8a"
      "\x17\xf4\x81\x48\x7e\x3a\xe8\x63\xc6\x78\x32\x54\x22\xe6\xf7\x8e\x16\x6d\x18"
      "\xaa\x7f\xd6\x36\x25\x8b\xce\x28\x72\x6f\x66\x1f\x73\x88\x93\xce\x44\x31\x1e"
      "\x4b\xe6\xc0\x53\x51\x93\xe5\xef\x72\xe8\x68\x62\x33\x72\x9c\x22\x7d\x82\x0c"
      "\x99\x94\x45\xd8\x92\x46\xc8\xc3\x59";

  static uint8_t server_fp_header[] = "\x46\x50\x4c\x59\x03\x01\x04\x00\x00\x00\x00\x14";

  resp->respondWith(200); // assume it's handled

  // uint8_t *out;
  // size_t out_len;
  int version_pos = 4;
  int mode_pos = 14;
  int type_pos = 5;
  int seq_pos = 6;
  int setup_message_type = 1;
  int setup1_message_seq = 1;
  int setup2_message_seq = 3;
  int setup2_suffix_len = 20;
  // int ret;

  // response and len are dummy values and can be ignored

  // debug(1, "Version: %02x, mode: %02x, type: %02x, seq: %02x", req->bodyData()[version_pos],
  //       req->bodyData()[mode_pos], req->bodyData()[type_pos], req->bodyData()[seq_pos]);

  if (req->bodyData()[version_pos] != 3 || req->bodyData()[type_pos] != setup_message_type) {
    debug(1, "Unsupported FP version.");
  }

  char *response = NULL;
  size_t len = 0;

  if (req->bodyData()[seq_pos] == setup1_message_seq) {
    // All replies are the same length. -1 to account for the NUL byte at the end.
    len = sizeof(server_fp_reply1) - 1;

    if (req->bodyData()[mode_pos] == 0)
      response = static_cast<char *>(memdup(server_fp_reply1, len));
    if (req->bodyData()[mode_pos] == 1)
      response = static_cast<char *>(memdup(server_fp_reply2, len));
    if (req->bodyData()[mode_pos] == 2)
      response = static_cast<char *>(memdup(server_fp_reply3, len));
    if (req->bodyData()[mode_pos] == 3)
      response = static_cast<char *>(memdup(server_fp_reply4, len));

  } else if (req->bodyData()[seq_pos] == setup2_message_seq) {
    // -1 to account for the NUL byte at the end.
    len = sizeof(server_fp_header) - 1 + setup2_suffix_len;
    response = static_cast<char *>(malloc(len));
    if (response) {
      memcpy(response, server_fp_header, sizeof(server_fp_header) - 1);
      memcpy(response + sizeof(server_fp_header) - 1,
             req->bodyData() + req->bodyLength() - setup2_suffix_len, setup2_suffix_len);
    }
  }

  if (response == NULL) {
    debug(1, "Cannot create a response.");
  }

  replaceBodyFromAllocation(*resp, response, len);
  resp->addHeader("Content-Type", "application/octet-stream");
}

/*
        <key>Identifier</key>
        <string>21cc689d-d5de-4814-872c-71d1426b57e0</string>
        <key>Enable_HK_Access_Control</key>
        <true/>
        <key>PublicKey</key>
        <data>
        qXJDhhL5F3OACL+HO7LVLQVdy0OJtavepjpF720PaOQ=
        </data>
        <key>Device_Name</key>
        <string>MyDevice</string>
        <key>Access_Control_Level</key>
        <integer>0</integer>
*/
void handle_configure(rtsp_conn_info *conn __attribute__((unused)),
                      RtspMessage *req __attribute__((unused)), RtspMessage *resp) {

  debug_log_rtsp_message_conn(conn, 1, "POST /configure req:", req);

  int existingEnable_HK_Access_Control = config.enable_HK_Access_Control;

  plist_t response_plist = plist_new_dict();

  // look for a configuration dictionary
  plist_t messagePlist = plistFromMessageBody(*req);
  if (messagePlist != NULL) {
    // look for the keyed dict "ConfigurationDictionary"
    plist_t configurationDict = plist_dict_get_item(messagePlist, "ConfigurationDictionary");
    if (configurationDict != NULL) {
      uint8_t enable_HK_Access_Control = 0;
      plist_t enableItem = plist_dict_get_item(configurationDict, "Enable_HK_Access_Control");
      if (enableItem != NULL) {
        plist_get_bool_val(enableItem, &enable_HK_Access_Control);

        if (enable_HK_Access_Control != 0) {
          config.enable_HK_Access_Control = 1;
          plist_dict_set_item(response_plist, "Identifier", plist_new_string(config.airplay_pi));
          plist_dict_set_item(response_plist, "Enable_HK_Access_Control", plist_new_bool(1));
          plist_dict_set_item(
              response_plist, "PublicKey",
              plist_new_data((const char *)config.airplay_pk, sizeof(config.airplay_pk)));
          plist_dict_set_item(response_plist, "Device_Name", plist_new_string(config.service_name));
          plist_dict_set_item(response_plist, "Access_Control_Level", plist_new_uint(0));
        } else {
          config.enable_HK_Access_Control = 0;
          // leave the response dict empty
        }
      } else {
        debug(1, "no Enable_HK_Access_Control item in POST /configure ConfigurationDictionary");
      }
      debug(1, "enable_HK_Access_Control is %u.", enable_HK_Access_Control);
    } else {
      debug(1, "no ConfigurationDictionary in POST /configure plist");
    }
    plist_free(messagePlist);
  } else {
    debug(1, "no plist in POST /configure request");
  }

  principalSession.withCurrent([](SessionState *) {
    if (config.enable_HK_Access_Control != 0)
      config.airplay_statusflags |= (1 << 10);
    else
      config.airplay_statusflags &= ~(1U << 10);
  });

  if (config.enable_HK_Access_Control != existingEnable_HK_Access_Control) {
    publishPrincipalSession();

  }
  replaceBodyWithPlist(*resp, response_plist);
  plist_free(response_plist);

  resp->addHeader("Content-Type", "application/x-apple-binary-plist");
  debug_log_rtsp_message_conn(conn, 1, "POST /configure response:", resp);
}

void handle_feedback(rtsp_conn_info *conn, __attribute__((unused)) RtspMessage *req,
                     __attribute__((unused)) RtspMessage *resp) {
  debug(4, "Connection %d: POST %s Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(4, NULL, req);

  int is_playing = 0;
  int connection_number = 0;
  int type = 0;
  double rate = 0.0;

  // get information from the current player, if any.

  const auto playing = principalSession.snapshot();
  if (playing.playing) {
    is_playing = 1;
    connection_number = *playing.id;
    type = playing.type;
    rate = playing.inputRate;
  }

  // debug(1, "Player is%s playing.", is_playing != 0 ? "" : " not");

  if (is_playing != 0) {
    if ((type != 96) && (type != 103))
      debug(1, "Connection %d, feedback unexpected type: %u.", connection_number, type);
    if ((rate != 44100.0) && (rate != 48000.0))
      debug(2, "Connection %d, feedback unexpected rate: %f.", connection_number, rate);
    plist_t payload_plist = plist_new_dict();
    plist_dict_set_item(payload_plist, "type", plist_new_uint(type));
    plist_dict_set_item(payload_plist, "sr", plist_new_real(rate));

    plist_t array_plist = plist_new_array();
    plist_array_append_item(array_plist, payload_plist);

    plist_t response_plist = plist_new_dict();
    plist_dict_set_item(response_plist, "streams", array_plist);

    replaceBodyWithPlist(*resp, response_plist);
    plist_free(response_plist);
    // plist_free(array_plist);
    // plist_free(payload_plist);

    resp->addHeader("Content-Type", "application/x-apple-binary-plist");
    debug_log_rtsp_message(4, "FEEDBACK response:", resp);
  }
}

void handle_command(rtsp_conn_info *conn, RtspMessage *req,
                    __attribute__((unused)) RtspMessage *resp) {
  debug(3, "Connection %d: POST %s Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(3, NULL, req);
  if (req->bodyStartsWith("bplist00")) {
    // we are not going to load the plist here because we don't wamt
    // to incur the memory and processing cost. So we'll just send it to the
    // metadata handling code and it can be dealt with there.
    /*
    plist_t command_dict = NULL;
    plist_from_memory(req->bodyData(), req->bodyLength(), &command_dict);
    if (command_dict != NULL) {
      // we have a plist -- try to get the dict item keyed to "updateMRSupportedCommands"
      plist_t item = plist_dict_get_item(command_dict, "type");
      if (item != NULL) {
        char *typeValue = NULL;
        plist_get_string_val(item, &typeValue);
        debug(1, "Connection %d: POST /command plist type \"%s\" received.",
              conn->connection_number, typeValue);
        debug_log_rtsp_message(1, NULL, req);
        if ((typeValue != NULL) && (strcmp(typeValue, "updateMRSupportedCommands") == 0)) {
          item = plist_dict_get_item(command_dict, "params");
          if (item != NULL) {
            // the item should be a dict
            plist_t item_array = plist_dict_get_item(item, "mrSupportedCommandsFromSender");
            if (item_array != NULL) {
              // here we have an array of data items
              uint32_t items = plist_array_get_size(item_array);
              if (items) {
                uint32_t item_number;
                for (item_number = 0; item_number < items; item_number++) {
                  plist_t the_item = plist_array_get_item(item_array, item_number);
                  char *buff = NULL;
                  uint64_t length = 0;
                  plist_get_data_val(the_item, &buff, &length);
                  // debug(1,"Item %d, length: %" PRId64 " bytes", item_number, length);
                  if ((buff != NULL) && (length >= strlen("bplist00")) &&
                      (strstr(buff, "bplist00") == buff)) {
                    // debug(1,"Contains a plist.");
                    plist_t subsidiary_plist = NULL;
                    plist_from_memory(buff, length, &subsidiary_plist);
                    if (subsidiary_plist) {
                      char *printable_plist = plist_as_xml_text(subsidiary_plist);
                      if (printable_plist) {
                        debug(4, "Connection %d:\n==\n%s\n==", conn->connection_number,
    printable_plist); free(printable_plist); } else { debug(1, "Can't print the plist!");
                      }
                      plist_free(subsidiary_plist);
                    } else {
                      debug(1, "Can't access the plist!");
                    }
                  }
                  if (buff != NULL)
                    free(buff);
                }
              }
            } else {
              debug(1, "Connection %d: POST /command no mrSupportedCommandsFromSender item.",
                    conn->connection_number);
            }
          } else {
            debug(1, "Connection %d: POST /command no params dict.", conn->connection_number);
          }
          resp->respondWith(200);
        }
        if (typeValue != NULL)
          free(typeValue);
      } else {
        debug(2, "Connection %d: Could not find a \"type\" item.", conn->connection_number);
      }

      plist_free(command_dict);
    } else {
      debug(1, "Connection %d: POST /command plist cannot be inputted.", conn->connection_number);
    }
    */
  } else {
    debug(1, "Connection %d: POST /command contains no plist", conn->connection_number);
  }
}

void handle_audio_mode(rtsp_conn_info *conn, RtspMessage *req,
                       __attribute__((unused)) RtspMessage *resp) {
  debug(2, "Connection %d: POST %s Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(3, NULL, req);
}

void handle_post(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  resp->respondWith(200);
  if (req->requestsPath("/pair-setup")) {
    handle_pair_setup(conn, req, resp);
  } else if (req->requestsPath("/pair-verify")) {
    handle_pair_verify(conn, req, resp);
  } else if (req->requestsPath("/pair-add")) {
    handle_pair_add(conn, req, resp);
  } else if (req->requestsPath("/pair-remove")) {
    handle_pair_remove(conn, req, resp);
  } else if (req->requestsPath("/pair-list")) {
    handle_pair_list(conn, req, resp);
  } else if (req->requestsPath("/pair-pin-start")) {
    handle_pair_pin_start(conn, req, resp);
  } else if (req->requestsPath("/fp-setup")) {
    handle_fp_setup(conn, req, resp);
  } else if (req->requestsPath("/configure")) {
    handle_configure(conn, req, resp);
  } else if (req->requestsPath("/feedback")) {
    handle_feedback(conn, req, resp);
  } else if (req->requestsPath("/command")) {
    handle_command(conn, req, resp);
  } else if (req->requestsPath("/audioMode")) {
    handle_audio_mode(conn, req, resp);
  } else {
    debug(1, "Connection %d: Unhandled POST %s Content-Length %d", conn->connection_number,
          req->requestPath(), req->bodyLength());
    debug_log_rtsp_message(2, "POST request", req);
    resp->respondWith(501);
  }
}

void handle_setpeers(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(2, "Connection %d: SETPEERS %s Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(3, "SETPEERS request", req);
  /*
    char timing_list_message[4096];
    timing_list_message[0] = 'T';
    timing_list_message[1] = 0;

    // ensure the client itself is first -- it's okay if it's duplicated later
    strncat(timing_list_message, " ", sizeof(timing_list_message) - 1 -
    strlen(timing_list_message)); strncat(timing_list_message, (const char
    *)&conn->client_ip_string, sizeof(timing_list_message) - 1 - strlen(timing_list_message));

    plist_t addresses_array = NULL;
    plist_from_memory(req->bodyData(), req->bodyLength(), &addresses_array);
    uint32_t items = plist_array_get_size(addresses_array);
    if (items) {
      uint32_t item;
      for (item = 0; item < items; item++) {
        plist_t n = plist_array_get_item(addresses_array, item);
        char *ip_address = NULL;
        plist_get_string_val(n, &ip_address);
        // debug(1,ip_address);
        strncat(timing_list_message, " ",
                sizeof(timing_list_message) - 1 - strlen(timing_list_message));
        strncat(timing_list_message, ip_address,
                sizeof(timing_list_message) - 1 - strlen(timing_list_message));
        if (ip_address != NULL)
          free(ip_address);
      }
      ptp_send_control_message_string(timing_list_message);
    }
    plist_free(addresses_array);
  */
  // set_client_as_ptp_clock(conn);
  resp->respondWith(200);
}
void handle_setpeersx(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(2, "Connection %d: SETPEERSX %s Content-Length %d", conn->connection_number, req->requestPath(),
        req->bodyLength());
  debug_log_rtsp_message(2, "SETPEERS Xrequest", req);
  resp->respondWith(200);
}



void handle_options_2(rtsp_conn_info *conn, __attribute__((unused)) RtspMessage *req,
                      RtspMessage *resp) {
  debug_log_rtsp_message_conn(conn, 3, "OPTIONS request", req);
  debug(3, "Connection %d: OPTIONS", conn->connection_number);
  resp->respondWith(200);
  resp->addHeader("Public",
                 "OPTIONS, SETUP, RECORD, FLUSH, FLUSHBUFFERED, TEARDOWN, "
                 "GET_PARAMETER, SET_PARAMETER, POST, GET, SETPEERS, SETPEERSX, "
                 "SETRATEANCHORTI, SETRATE");
}

void handle_teardown_2(rtsp_conn_info *conn, __attribute__((unused)) RtspMessage *req,
                       RtspMessage *resp) {

  debug(4, "Connection %d from \"%s\": TEARDOWN (AP2 %s) %s Content-Length %d", conn->connection_number, conn->ap2_client_name, get_category_string(conn->airplay_stream_category), req->requestPath(), req->bodyLength());
  debug_log_rtsp_message_conn(conn, 4, "TEARDOWN (AP2)", req);
  // look for a configuration dictionary

  plist_t messagePlist = plistFromMessageBody(*req);
  if (messagePlist != NULL) {
    plist_t streams = plist_dict_get_item(messagePlist, "streams");
    if (streams != NULL) {
      // just drop the player, leave the connection open
      if (player_stop(conn) == 0) {
        debug(4, "Connection %d from \"%s\": TEARDOWN (AP2 %s) %s Content-Length %d is stopping a player thread", conn->connection_number, conn->ap2_client_name, get_category_string(conn->airplay_stream_category), req->requestPath(), req->bodyLength());
        activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
      }
    } else {
      if (plist_dict_get_size(messagePlist) != 0) {
        debug(1, "Connection %d from \"%s\": TEARDOWN (AP2 %s) %s Content-Length %d plist is non-empty but contains no \"streams\" item.", conn->connection_number, conn->ap2_client_name, get_category_string(conn->airplay_stream_category), req->requestPath(), req->bodyLength());
        debug_log_rtsp_message_conn(conn, 4, "Contents follow:", req);
      }
      resp->addHeader("Connection", "close");
      debug(4, "Connection %d from \"%s\": TEARDOWN (AP2 %s) %s Content-Length %d is asking to terminate the connection.", conn->connection_number, conn->ap2_client_name, get_category_string(conn->airplay_stream_category), req->requestPath(), req->bodyLength());
      conn->stop = 1;
    }
    plist_free(messagePlist);
  } else {
    debug(1, "Connection %d from \"%s\": TEARDOWN (AP2 %s) %s Content-Length %d has no plist -- nothing done.", conn->connection_number, conn->ap2_client_name, get_category_string(conn->airplay_stream_category), req->requestPath(), req->bodyLength());
  }
  resp->respondWith(200);
}

void handle_flush(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug_log_rtsp_message(2, "FLUSH request", req);
  debug(3, "Connection %d: FLUSH", conn->connection_number);
  const char *p = NULL;
  uint32_t rtptime = 0;
  const char *hdr = req->headerValue("RTP-Info");

  if (hdr) {
    // debug(1,"FLUSH message received: \"%s\".",hdr);
    // get the rtp timestamp
    p = strstr(hdr, "rtptime=");
    if (p) {
      p = strchr(p, '=');
      if (p)
        rtptime = uatoi(p + 1); // unsigned integer -- up to 2^32-1
    }
  }
  debug(2, "RTSP Flush Requested: %u.", rtptime);
  if ((conn != NULL) && principalSession.isCurrent(conn->connection_number)) {

    player_flush(rtptime, conn); // will not crash even it there is no player thread.
    resp->respondWith(200);

  } else {
    warn("Connection %d FLUSH %u received without having the player", conn->connection_number,
         rtptime);
    resp->respondWith(451);
  }
}



void handle_setup_2(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  int err;

  debug(4, "Connection %d from \"%s\": SETUP (AP2) %s Content-Length %d", conn->connection_number, conn->ap2_client_name, req->requestPath(), req->bodyLength());
  debug_log_rtsp_message_conn(conn, 4, "SETUP (AP2)", req);

  plist_t messagePlist = plistFromMessageBody(*req);

  if (messagePlist != NULL) {
    // if (conn->sessionPlist)
    //   plist_free(conn->sessionPlist);
    conn->sessionPlist = messagePlist;
  }
  plist_t setupResponsePlist = plist_new_dict();
  resp->respondWith(501);

  // see if we can get a name for the client

  plist_t nameItem = plist_dict_get_item(messagePlist, "name");
  if (nameItem != NULL) {
    if (conn->ap2_client_name)
      free(conn->ap2_client_name);
    plist_get_string_val(nameItem, &conn->ap2_client_name); // we need to free it!
  }

  // see if the incoming plist contains a "streams" array
  plist_t streams = plist_dict_get_item(messagePlist, "streams");
  if (streams == NULL) {
    // no "streams" plist, so it must (?) be an initial setup
    debug(2,
          "Connection %d SETUP: No \"streams\" array has been found -- create an event thread "
          "and open a TCP port.",
          conn->connection_number);
    conn->airplay_stream_category = unspecified_stream_category;

    // figure out what category of stream it is, by looking at the plist
    plist_t timingProtocol = plist_dict_get_item(messagePlist, "timingProtocol");
    if (timingProtocol != NULL) {
      char *timingProtocolString = NULL;
      plist_get_string_val(timingProtocol, &timingProtocolString);
      if (timingProtocolString) {
        if (strcmp(timingProtocolString, "PTP") == 0) {
          debug(2, "Connection %d: AP2 PTP connection from %s:%u (\"%s\") to self at %s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                conn->ap2_client_name, conn->self_ip_string, conn->self_rtsp_port);
          conn->airplay_stream_category = ptp_stream;

          do_pthread_setname(&conn->thread, "ap2_ptp_%d", conn->connection_number);

        } else if (strcmp(timingProtocolString, "NTP") == 0) {
  free(timingProtocolString);
  plist_free(setupResponsePlist);
  plist_free(messagePlist);
  conn->sessionPlist = NULL;
  resp->respondWith(400);
  return;
} else if (strcmp(timingProtocolString, "None") == 0) {
          debug(3,
                "Connection %d: SETUP: a \"None\" setup detected from %s:%u (\"%s\") to self at "
                "%s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                conn->ap2_client_name, conn->self_ip_string, conn->self_rtsp_port);
          // now check to see if it's got the "isRemoteControlOnly" item and check it's true
          plist_t isRemoteControlOnly = plist_dict_get_item(messagePlist, "isRemoteControlOnly");
          if (isRemoteControlOnly != NULL) {
            uint8_t isRemoteControlOnlyBoolean = 0;
            plist_get_bool_val(isRemoteControlOnly, &isRemoteControlOnlyBoolean);
            if (isRemoteControlOnlyBoolean != 0) {
              debug(2,
                    "Connection %d: SETUP: Remote Control Only connection from %s:%u (\"%s\") to "
                    "self at %s:%u.",
                    conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                    conn->ap2_client_name, conn->self_ip_string, conn->self_rtsp_port);
              conn->airplay_stream_category = remote_control_stream;
              do_pthread_setname(&conn->thread, "ap2_rc_%d", conn->connection_number);
            } else {
              debug(1,
                    "Connection %d: SETUP: a \"None\" setup detected, with "
                    "\"isRemoteControlOnly\" item set to \"false\".",
                    conn->connection_number);
            }
          } else {
            debug(1,
                  "Connection %d: SETUP: a \"None\" setup detected, but no "
                  "\"isRemoteControlOnly\" item detected.",
                  conn->connection_number);
          }
        }

        debug(2,
              "Connection %d from \"%s\": Initial (i.e. no streams array) SETUP (AirPlay 2) on %s",
              conn->connection_number, conn->ap2_client_name,
              get_category_string(conn->airplay_stream_category));
        debug_log_rtsp_message_conn(
            conn, 2, "Initial (i.e. no streams array) SETUP (AirPlay 2) incoming message", req);

        // here, we know it's an initial setup and we know the kind of setup being requested
        // if it's a full service PTP stream, we get groupUUID, groupContainsGroupLeader and
        // timingPeerList
        if (conn->airplay_stream_category == ptp_stream) {

          // airplay 2 always allows interruption, so should never return
          // play_lock_aquisition_failed
          if (get_play_lock(conn, 1) != play_lock_aquisition_failed) {
            debug(2, "Connection %d: %s AP2 setup -- play lock acquired.", conn->connection_number,
                  get_category_string(conn->airplay_stream_category));


            if (ptp_shm_interface_open() !=
                0) // it should be open already, but just in case it isn't...
              die("Can not access the NQPTP service. Has it stopped running?");
            debug_log_rtsp_message(3, "SETUP \"PTP\" message", req);
            plist_t groupUUID = plist_dict_get_item(messagePlist, "groupUUID");
            if (groupUUID) {
              char *gid = NULL;
              plist_get_string_val(groupUUID, &gid);
              if (gid) {
                principalSession.mutateSession(*conn, [gid](SessionState &session) {
                  free(session.airplay_gid);
                  session.airplay_gid = gid;
                });
              } else {
                debug(1, "Invalid groupUUID");
              }
            } else {
              debug(1, "No groupUUID in SETUP");
            }

            // now see if the group contains a group leader
            plist_t groupContainsGroupLeader =
                plist_dict_get_item(messagePlist, "groupContainsGroupLeader");
            if (groupContainsGroupLeader) {
              uint8_t value = 0;
              plist_get_bool_val(groupContainsGroupLeader, &value);
              principalSession.mutateSession(*conn, [value](SessionState &session) {
                session.groupContainsGroupLeader = value;
              });
              debug(3, "Updated groupContainsGroupLeader to %u", conn->groupContainsGroupLeader);
            } else {
              debug(1, "No groupContainsGroupLeader in SETUP");
            }

            char timing_list_message[4096];
            timing_list_message[0] = 'T';
            timing_list_message[1] = 0;

            // ensure the client itself is first -- it's okay if it's duplicated later
            strncat(timing_list_message, " ",
                    sizeof(timing_list_message) - 1 - strlen(timing_list_message));
            strncat(timing_list_message, (const char *)&conn->client_ip_string,
                    sizeof(timing_list_message) - 1 - strlen(timing_list_message));

            plist_t timing_peer_info = plist_dict_get_item(messagePlist, "timingPeerInfo");
            if (timing_peer_info) {
              // first, get the incoming plist.
              plist_t addresses_array = plist_dict_get_item(timing_peer_info, "Addresses");
              if (addresses_array) {
                // iterate through the array of items
                uint32_t items = plist_array_get_size(addresses_array);
                if (items) {
                  uint32_t item;
                  for (item = 0; item < items; item++) {
                    plist_t n = plist_array_get_item(addresses_array, item);
                    char *ip_address = NULL;
                    plist_get_string_val(n, &ip_address);
                    // debug(1, "Timing peer: %s", ip_address);
                    // plist_get_string_val() leaves ip_address NULL if the array item
                    // is not a string; skip it rather than passing NULL to strncat.
                    if (ip_address != NULL) {
                      strncat(timing_list_message, " ",
                              sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                      strncat(timing_list_message, ip_address,
                              sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                      free(ip_address);
                    }
                  }
                } else {
                  debug(1, "SETUP on Connection %d: No timingPeerInfo addresses in the array.",
                        conn->connection_number);
                }
              } else {
                debug(1, "SETUP on Connection %d: Can't find timingPeerInfo addresses",
                      conn->connection_number);
              }
              // make up the timing peer info list part of the response...
              // debug(1,"Create timingPeerInfoPlist");
              plist_t timingPeerInfoPlist = plist_new_dict();
              plist_t addresses = plist_new_array(); // to hold the device's interfaces
              plist_array_append_item(addresses, plist_new_string(conn->self_ip_string));
              //            debug(1,"self ip: \"%s\"", conn->self_ip_string);

              int oldState;
              pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
              struct ifaddrs *addrs, *iap;
              getifaddrs(&addrs);
              for (iap = addrs; iap != NULL; iap = iap->ifa_next) {
                // debug(1, "Interface index %d, name: \"%s\"",if_nametoindex(iap->ifa_name),
                // iap->ifa_name);
                if ((iap->ifa_addr) && (iap->ifa_netmask) && (iap->ifa_flags & IFF_UP) &&
                    ((iap->ifa_flags & IFF_LOOPBACK) == 0)) {
                  char buf[INET6_ADDRSTRLEN + 1]; // +1 for a NUL
                  memset(buf, 0, sizeof(buf));
                  if (iap->ifa_addr->sa_family == AF_INET6) {
                    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)(iap->ifa_addr);
                    inet_ntop(AF_INET6, (void *)&addr6->sin6_addr, buf, sizeof(buf));
                    plist_array_append_item(addresses, plist_new_string(buf));
                    // debug(1, "Own address IPv6: %s", buf);

                    // strncat(timing_list_message, " ",
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                    // strncat(timing_list_message, buf,
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));

                  } else {
                    struct sockaddr_in *addr = (struct sockaddr_in *)(iap->ifa_addr);
                    inet_ntop(AF_INET, (void *)&addr->sin_addr, buf, sizeof(buf));
                    plist_array_append_item(addresses, plist_new_string(buf));
                    // debug(1, "Own address IPv4: %s", buf);

                    // strncat(timing_list_message, " ",
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                    // strncat(timing_list_message, buf,
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                  }
                }
              }
              freeifaddrs(addrs);
              pthread_setcancelstate(oldState, NULL);

              // debug(1,"initial timing peer command: \"%s\".", timing_list_message);
              // ptp_send_control_message_string(timing_list_message);
              // deferring this until play is about to start
              // set_client_as_ptp_clock(conn);
              // ptp_send_control_message_string("B"); // signify clock dependability period is
              // "B"eginning (or continuing)
              plist_dict_set_item(timingPeerInfoPlist, "Addresses", addresses);
              plist_dict_set_item(timingPeerInfoPlist, "ID",
                                  plist_new_string(conn->self_ip_string));
              plist_dict_set_item(setupResponsePlist, "timingPeerInfo", timingPeerInfoPlist);
              // get a port to use as an event port
              // bind a new TCP port and get a socket
              conn->local_event_port = 0; // any port
              int eventListener = -1;
              int lerr = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family,
                                              conn->self_ip_string, conn->self_scope_id,
                                              &conn->local_event_port, &eventListener);
              if (lerr) {
                die("SETUP on Connection %d: Error %d: could not find a TCP port to use as an "
                    "event "
                    "port",
                    conn->connection_number, lerr);
              }

              const auto eventStarted = start_ap2_event_worker(conn,eventListener);
              if (!eventStarted)
                die("SETUP on Connection %d: could not start event listener: %d",
                    conn->connection_number,eventStarted.error());

              debug(2, "Connection %d: TCP PTP event port opened: %u.", conn->connection_number,
                    conn->local_event_port);

              plist_dict_set_item(setupResponsePlist, "eventPort",
                                  plist_new_uint(conn->local_event_port));
              plist_dict_set_item(setupResponsePlist, "timingPort", plist_new_uint(0)); // dummy
              // cancel_all_RTSP_threads(ptp_stream,
              //                         conn->connection_number); // kill all the other listeners
              resp->respondWith(200);
            } else {
              debug(1, "SETUP on Connection %d: PTP setup -- no timingPeerInfo plist.",
                    conn->connection_number);
            }

            // since the GID from the client has been acquired, update the airplay bonjour strings.
            publishPrincipalSession();
            debug(2, "Connection %d: SETUP mdns_update on %s.", conn->connection_number,
                  get_category_string(conn->airplay_stream_category));



          } else {
            // this should never happen!
            debug(1, "SETUP on Connection %d: could not become principal conn.",
                  conn->connection_number);
            resp->respondWith(453);
          }
        } else if (conn->airplay_stream_category == remote_control_stream) {

          debug_log_rtsp_message(3, "SETUP (no stream) \"isRemoteControlOnly\" message", req);

          // get a port to use as an event port
          // bind a new TCP port and get a socket
          conn->local_event_port = 0; // any port
          int eventListener = -1;
          int lerr = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family,
                                          conn->self_ip_string, conn->self_scope_id,
                                          &conn->local_event_port, &eventListener);
          if (lerr) {
            die("SETUP on Connection %d: Error %d: could not find a TCP port to use as an event "
                "port",
                conn->connection_number, lerr);
          }

          const auto eventStarted = start_ap2_event_worker(conn,eventListener);
          if (!eventStarted)
            die("SETUP on Connection %d: could not start event listener: %d",
                conn->connection_number,eventStarted.error());

          debug(2, "Connection %d SETUP (RC): TCP Remote Control event port opened: %u.",
                conn->connection_number, conn->local_event_port);
          plist_dict_set_item(setupResponsePlist, "eventPort",
                              plist_new_uint(conn->local_event_port));
          debug(2, "SETUP on Connection %d: RemoteControl Only eventPort %u.",
                conn->connection_number, conn->local_event_port);
          // plist_dict_set_item(setupResponsePlist, "timingPort", plist_new_uint(0));
          //  cancel_all_RTSP_threads(
          //     remote_control_stream,
          //     conn->connection_number); // kill all the other remote control listeners

          resp->respondWith(200);
        } else {
          debug(1, "SETUP on Connection %d: an unrecognised \"%s\" setup detected.",
                conn->connection_number, timingProtocolString);
          warn("Shairport Sync can not handle streams of this type: \"%s\".", timingProtocolString);
        }
        free(timingProtocolString);
      } else {
        debug(1, "SETUP on Connection %d: Can't retrieve timingProtocol string in initial SETUP.",
              conn->connection_number);
      }
    } else {
      debug(1,
            "SETUP on Connection %d: Unrecognised SETUP incoming message from \"%s\": no "
            "timingProtocol or streams plist found.",
            conn->connection_number, (const char *)conn->client_ip_string);
      debug_log_rtsp_message(1, "Unrecognised SETUP incoming message.", req);
      warn("Unrecognised SETUP incoming message -- ignored.");
    }
  } else {

    if (conn->airplay_stream_category == ptp_stream) {

      if (player_stop(conn) == 0) {
        debug(1, "stopping a running player during setup phase 2");
        activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
      }

      set_client_as_ptp_clock(conn);
      ptp_send_control_message_string(
          "B"); // signify clock dependability period is "B"eginning (or continuing)
      plist_t stream0 = plist_array_get_item(streams, 0);

      plist_t streams_array = plist_new_array(); // to hold the ports and stuff
      plist_t stream0dict = plist_new_dict();

      // get the session key -- it must have one

      plist_t item = plist_dict_get_item(stream0, "shk"); // session key
      uint64_t item_value = 0;
      if (item != NULL) {
        plist_get_data_val(item, (char **)&conn->session_key,
                           &item_value); // item_value is the session key length (?)
        // The session key is used later as a fixed 32-byte ChaCha20-Poly1305-IETF
        // key (see rtp.c). Reject any other length instead of reading past the end
        // of a short key on every packet decrypt.
        if (item_value != 32) {
          warn("Connection %d: SETUP \"shk\" session key length is %" PRIu64
               ", not 32 bytes; ignoring it.",
               conn->connection_number, item_value);
          if (conn->session_key != NULL) {
            free(conn->session_key);
            conn->session_key = NULL;
          }
        }
      } else {
        warn("No session key (shk) property in setup! This is fatal!");
      }

      // get the compression type
      // this seems to be static -- a stream's encoding can change dynamically, it seems
      item = plist_dict_get_item(stream0, "ct"); // compression type
      if (item != NULL) {
        plist_get_uint_val(item, &item_value);
        conn->compressionType = item_value;
        // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
      } else {
        debug(1, "No compression type (ct) property found in setup.");
      }

      // get the max frames per packet
      item = plist_dict_get_item(stream0, "spf"); // samples per frame (?)
      if (item != NULL) {
        plist_get_uint_val(item, &item_value);
        // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
        debug(3, "Frames per packet (aka spf (\"samples per frame\"?): %" PRId64 ".", item_value);
        conn->inputAudio.setSetupPacketFrames(item_value);
      } else {
        warn("No frames per packet (spf) property found in setup!");
      }

      // bind a new UDP port and get a socket
      conn->local_ap2_control_port = 0; // any port
      int controlDescriptor = -1;
      err = bind_socket_and_port(SOCK_DGRAM, conn->connection_ip_family, conn->self_ip_string,
                                 conn->self_scope_id, &conn->local_ap2_control_port,
                                 &controlDescriptor);
      if (!err) {
        err = conn->controlChannel.adopt(controlDescriptor);
        if (err) close(controlDescriptor);
      }
      if (err) {
        die("Error %d: could not find a UDP port to use as an ap2_control port", err);
      }
      debug(2, "Connection %d: UDP control port opened: %u.", conn->connection_number,
            conn->local_ap2_control_port);

      named_pthread_create(&conn->rtp_ap2_control_thread, NULL, &rtp_ap2_control_receiver,
                           (void *)conn, "ap2_cn_%d", conn->connection_number);

      // get the DACP-ID and Active Remote for remote control stuff

      const char *ar = req->headerValue("Active-Remote");
      if (ar) {
        debug(3, "Connection %d: SETUP AP2 -- Active-Remote string seen: \"%s\".",
              conn->connection_number, ar);
        // get the active remote
        if (conn->dacp_active_remote) // this is in case SETUP was previously called
          free(conn->dacp_active_remote);
        conn->dacp_active_remote = strdup(ar);
      } else {
        debug(2, "Connection %d: SETUP AP2 no Active-Remote information in the the SETUP Record.",
              conn->connection_number);
        if (conn->dacp_active_remote) { // this is in case SETUP was previously called
          free(conn->dacp_active_remote);
          conn->dacp_active_remote = NULL;
        }
      }

      ar = req->headerValue("DACP-ID");
      if (ar) {
        debug(3, "Connection %d: SETUP AP2 -- DACP-ID string seen: \"%s\".",
              conn->connection_number, ar);
        if (conn->dacp_id) // this is in case SETUP was previously called
          free(conn->dacp_id);
        conn->dacp_id = strdup(ar);
      } else {
        debug(2, "Connection %d: SETUP AP2 doesn't include DACP-ID string information.",
              conn->connection_number);
        if (conn->dacp_id) { // this is in case SETUP was previously called
          free(conn->dacp_id);
          conn->dacp_id = NULL;
        }
      }

      // now, get the type of the stream.
      item = plist_dict_get_item(stream0, "type");
      item_value = 0;
      plist_get_uint_val(item, &item_value);
      conn->type = item_value;
      switch (item_value) {
      case 96: {
        debug(4, "Connection %d. AP2 Realtime Audio Stream SETUP.", conn->connection_number);
        debug_log_rtsp_message(4, "AP2 Realtime Audio Stream SETUP incoming message:", req);

        conn->airplay_stream_type = realtime_stream;
        // get the sample rate
        item = plist_dict_get_item(stream0, "sr"); // sample rate
        if (item != NULL) {
          plist_get_uint_val(item, &item_value);
          // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
          conn->inputAudio.setSetupSampleRate(item_value);
          debug(4, "Set conn->input_rate: %u.", conn->inputAudio.sampleRate());
        } else {
          debug(1, "Connection %d. No sample rate (sr) property found in setup.",
                conn->connection_number);
        }

        item = plist_dict_get_item(stream0, "spf"); // samples per frame
        if (item != NULL) {
          plist_get_uint_val(item, &item_value);
          conn->inputAudio.setSetupPacketFrames(item_value);
          debug(4, "Set conn->frames_per_packet: %u.", conn->inputAudio.framesPerPacket());
        } else {
          debug(1, "Connection %d. No samples per frame (spf) property found in setup.",
                conn->connection_number);
        }

        // bind a new UDP port and get a socket
        conn->local_realtime_audio_port = 0; // any port
        err = bind_socket_and_port(SOCK_DGRAM, conn->connection_ip_family, conn->self_ip_string,
                                   conn->self_scope_id, &conn->local_realtime_audio_port,
                                   &conn->realtime_audio_socket);
        if (err) {
          die("Error %d: could not find a UDP port to use as a realtime_audio port", err);
        }
        debug(2, "Connection %d: UDP realtime audio port opened: %u.", conn->connection_number,
              conn->local_realtime_audio_port);

        named_pthread_create(&conn->rtp_realtime_audio_thread, NULL, &rtp_realtime_audio_receiver,
                             (void *)conn, "ap2_ra_%d", conn->connection_number);
        plist_dict_set_item(stream0dict, "type", plist_new_uint(96));
        plist_dict_set_item(stream0dict, "dataPort",
                            plist_new_uint(conn->local_realtime_audio_port));

        debug(2, "Realtime Stream Play");
        activity_monitor_signify_activity(1);
        player_play(conn);

        conn->controlChannel.activate();
      } break;
      case 103: {
        debug_log_rtsp_message(3, "Buffered Audio Stream SETUP incoming message", req);
        conn->airplay_stream_type = buffered_stream;

        // get the audio format code
        item = plist_dict_get_item(stream0, "audioFormat"); // audio format
        if (item != NULL) {
          plist_get_uint_val(item, &conn->audio_format);
          // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
          // seems to be only the initial format -- it seems as if it can change dynamically
        } else {
          debug(1, "No audio format (audioFormat) property found in setup.");
        }

        // bind a new TCP port and get a socket
        conn->local_buffered_audio_port = 0; // any port
        err = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family, conn->self_ip_string,
                                   conn->self_scope_id, &conn->local_buffered_audio_port,
                                   &conn->buffered_audio_socket);
        if (err) {
          die("SETUP on Connection %d: Error %d: could not find a TCP port to use as a "
              "buffered_audio port",
              conn->connection_number, err);
        }

        const auto bufferedFlags = fcntl(conn->buffered_audio_socket, F_GETFL);
        if (bufferedFlags < 0 || fcntl(conn->buffered_audio_socket, F_SETFL, bufferedFlags | O_NONBLOCK) < 0 ||
            listen(conn->buffered_audio_socket, 128) < 0)
          die("SETUP on Connection %d: cannot prepare buffered TCP listener: %d.",
              conn->connection_number, errno);

        debug(2, "Connection %d: TCP Buffered Audio port opened: %u.", conn->connection_number,
              conn->local_buffered_audio_port);

        activity_monitor_signify_activity(1);

        // debug(1, "Connection %d: create rtp_buffered_audio_thread", conn->connection_number);

        named_pthread_create_with_priority(&conn->rtp_buffered_audio_thread, 2,
                                           &rtp_buffered_audio_processor, (void *)conn, "ap2_ba_%d",
                                           conn->connection_number);

        plist_dict_set_item(stream0dict, "type", plist_new_uint(103));
        plist_dict_set_item(stream0dict, "dataPort",
                            plist_new_uint(conn->local_buffered_audio_port));
        plist_dict_set_item(stream0dict, "audioBufferSize",
                            plist_new_uint(conn->ap2_audio_buffer_size));

        // this should be cancelled by an activity_monitor_signify_activity(1)
        // call in the SETRATEANCHORI handler, which should come up right away
        activity_monitor_signify_activity(0);
        player_play(conn);
        conn->controlChannel.activate();
      } break;
      case 130: {
        debug(1, "Remote Control Setup Received on a PTP connection.");
        debug_log_rtsp_message(2, "Incoming message", req);
      } break;
      default:
        debug(1, "SETUP on Connection %d: Unhandled stream type %" PRIu64 ".",
              conn->connection_number, item_value);
        debug_log_rtsp_message(1, "Unhandled stream type incoming message", req);
      }

      plist_dict_set_item(stream0dict, "controlPort", plist_new_uint(conn->local_ap2_control_port));

      plist_array_append_item(streams_array, stream0dict);
      plist_dict_set_item(setupResponsePlist, "streams", streams_array);

      resp->respondWith(200);
    } else if (conn->airplay_stream_category == remote_control_stream) {
      debug(3, "Connection %d (RC): SETUP: Remote Control Only with stream received from %s.",
            conn->connection_number, conn->client_ip_string);
      debug_log_rtsp_message(3, "Remote Control Stream SETUP incoming message", req);

      plist_t seed_item = NULL;
      // the data port and listener thread may already have been set up
      // if so, the local_data_port will be non-zero

      if (conn->local_data_port == 0) {
        // set up data channel ciphering
        plist_t dict = plist_array_get_item(streams, 0);
        if (dict != NULL) {
          // get the seed that becomes the suffix for the salt
          seed_item = plist_dict_get_item(dict, "seed"); // session key
          uint64_t seed = 0;
          if (seed_item != NULL) {
            plist_get_uint_val(seed_item, &seed);
            char salt_suffix[256] = "";
            snprintf(salt_suffix, sizeof(salt_suffix), "%" PRIu64 "", seed);
            conn->ap2_pairing_context.data_cipher_bundle.cipher_ctx =
                pair_cipher_new(PAIR_SERVER_HOMEKIT, 5, conn->pair_setup_result->shared_secret,
                                conn->pair_setup_result->shared_secret_len,
                                salt_suffix); // last argument is the (possible) dynamic salt suffix
            if (conn->ap2_pairing_context.data_cipher_bundle.cipher_ctx != NULL) {
              conn->ap2_pairing_context.data_cipher_bundle.description = strdup("DataStream");
              // get a port to use as an data port
              // bind a new TCP port and get a socket
              conn->local_data_port = 0; // any port
              int lerr = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family,
                                              conn->self_ip_string, conn->self_scope_id,
                                              &conn->local_data_port, &conn->data_socket);
              if (lerr) {
                die("SETUP on Connection %d (RC): Error %d: could not find a TCP port to use as a "
                    "data "
                    "port",
                    conn->connection_number, lerr);
              }
              listen(conn->data_socket,
                     128); // open port for listening before telling the client about it!

              debug(2, "Connection %d SETUP (RC): TCP Remote Control data port opened: %u.",
                    conn->connection_number, conn->local_data_port);
            } else {
              debug(1, "Connection %d: SETUP: Error setting up rtsp data channel ciphering.",
                    conn->connection_number);
            }
          } else {
            debug(2, "Connection %d: SETUP: No data channel encryption salt seed found.",
                  conn->connection_number);
          }
        } else {
          debug(1, "Connection %d: SETUP: Could not find the streams array",
                conn->connection_number);
        }
      } else {
        debug(1, "Connection %d SETUP (RC): data port already allocated.", conn->connection_number);
      }
      plist_t coreResponseDict = plist_new_dict();
      plist_dict_set_item(coreResponseDict, "streamID", plist_new_uint(1));
      plist_dict_set_item(coreResponseDict, "type", plist_new_uint(130));
      if (seed_item != NULL)
        plist_dict_set_item(coreResponseDict, "dataPort", plist_new_uint(conn->local_data_port));

      plist_t coreResponseArray = plist_new_array();
      plist_array_append_item(coreResponseArray, coreResponseDict);
      plist_dict_set_item(setupResponsePlist, "streams", coreResponseArray);

      resp->respondWith(200);
    } else {
      debug(1, "Connection %d: SETUP: Stream received but no airplay category set. Nothing done.",
            conn->connection_number);
    }
  }

  if (resp->hasResponseCode(200)) {
    replaceBodyWithPlist(*resp, setupResponsePlist);
    plist_free(setupResponsePlist);
    resp->addHeader("Content-Type", "application/x-apple-binary-plist");
  }
  plist_free(messagePlist);
}


/*
static void handle_ignore(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(1, "Connection thread %d: IGNORE", conn->connection_number);
  resp->respondWith(200);
}
*/

class RuntimeParameterVolumePort : public ParameterVolumePort {
public:
  explicit RuntimeParameterVolumePort(rtsp_conn_info &session) : session_(session) {}
  AirPlayVolume suggestedVolume() override { return suggestedSessionVolume(&session_); }
  void acceptVolume(AirPlayVolume volume) override {
    debug(3, "Connection %d: request to set AirPlay Volume to: %f.", session_.connection_number,
          volume.value());
    session_.volumeControl.rememberLevel(volume);
    if (const auto ticket = principalSession.ticketFor(session_.connection_number)) {
      command_set_volume(volume.value());
      applySessionVolumeEffects(volume, session_, [&] {
        principalSession.commitIfSelected(*ticket, [&] { sharedVolumeLevel.remember(volume); });
      });
    }
  }

private:
  rtsp_conn_info &session_;
};

static void handle_get_parameter(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  RuntimeParameterVolumePort volume(*conn);
  RtspParameterHandler handler(volume);
  if (const auto reportedVolume = handler.get(*req, *resp))
    debug(2, "Connection %d: current volume (%.6f) requested", conn->connection_number,
          reportedVolume->value());
}

static void handle_set_parameter(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  debug(4, "Connection %d: SET_PARAMETER", conn->connection_number);
  RuntimeParameterVolumePort volume(*conn);
  RtspParameterHandler handler(volume);
  const auto diagnostics = handler.set(*req, *resp);
  if (diagnostics.content == ParameterContent::text) {
    debug(3, "received parameters in SET_PARAMETER request.");
    for (const auto &parameter : diagnostics.unrecognizedParameters)
      debug(1, "Connection %d, unrecognised parameter: \"%s\"\n", conn->connection_number,
            parameter.c_str());
  } else if (diagnostics.content == ParameterContent::unknown) {
    debug(1, "Connection %d: received unknown Content-Type \"%s\" in SET_PARAMETER request.",
          conn->connection_number, req->headerValue("Content-Type"));
    debug_print_msg_headers(1, req);
  } else if (diagnostics.content == ParameterContent::missing) {
    debug(1, "Connection %d: missing Content-Type header in SET_PARAMETER request.",
          conn->connection_number);
  }
}


static const struct method_handler {
  const char *method;
  void (*handler)(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp);
} method_handlers[] = {{"OPTIONS", handle_options_2},
                       {"FLUSH", handle_flush},
                       {"TEARDOWN", handle_teardown_2},
                       {"SETUP", handle_setup_2},
                       {"GET_PARAMETER", handle_get_parameter},
                       {"SET_PARAMETER", handle_set_parameter},
                       {"RECORD", handle_record_2},
                       {"GET", handle_get},
                       {"POST", handle_post},
                       {"SETPEERS", handle_setpeers},
                       {"SETPEERSX", handle_setpeersx},
                       {"SETRATEANCHORTI", handle_setrateanchori},
                       {"FLUSHBUFFERED", handle_flushbuffered},
                       {"SETRATE", handle_setrate},
                       {NULL, NULL}};

void rtsp_dispatch_request(rtsp_conn_info *conn, RtspMessage *req, RtspMessage *resp) {
  resp->respondWith(501);
  for (const struct method_handler *method = method_handlers; method->method; method++) {
    if (req->requestsMethod(method->method)) {
      method->handler(conn, req, resp);
      return;
    }
  }
}




void rtsp_conversation_thread_cleanup_function(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  if (conn != NULL) {
    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
    release_play_lock(conn);
    debug(3, "Connection %d: %s rtsp_conversation_thread_func_cleanup_function called.",
          conn->connection_number, get_category_string(conn->airplay_stream_category));

    if (player_stop(conn) == 0) {
      activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
    }

    if (conn->fd > 0) {
      debug(
          2,
          "Connection %d: terminating -- closing RTSP connection socket %d: from %s:%u to self at "
          "%s:%u.",
          conn->connection_number, conn->fd, conn->client_ip_string, conn->client_rtsp_port,
          conn->self_ip_string, conn->self_rtsp_port);
      safe_socket_close(&conn->fd);
    }

    if (conn->session_key) {
      free(conn->session_key);
      conn->session_key = NULL;
    }

    stop_ap2_event_worker(conn);
    debug(3, "Connection %d: %s event thread deleted.", conn->connection_number,
          get_category_string(conn->airplay_stream_category));

    debug(3, "Connection %d: terminating  -- closing timing, control and audio sockets...",
          conn->connection_number);

    conn->ap2_pairing_context.control_cipher_bundle.release();
    conn->eventCommandExchange.close();
    conn->ap2_pairing_context.event_cipher_bundle.release();
    conn->ap2_pairing_context.data_cipher_bundle.release();

    pair_setup_free(conn->ap2_pairing_context.setup_ctx);
    pair_verify_free(conn->ap2_pairing_context.verify_ctx);
    if (conn->airplay_gid) {
      free(conn->airplay_gid);
      conn->airplay_gid = NULL;
    }

    rtp_terminate(conn);

    if (conn->dacp_id) {
      free(conn->dacp_id);
      conn->dacp_id = NULL;
    }

    if (conn->UserAgent) {
      free(conn->UserAgent);
      conn->UserAgent = NULL;
    }

    if (conn->ap2_client_name) {
      free(conn->ap2_client_name);
      conn->ap2_client_name = NULL;
    }
    // remove flow control and mutexes
    int rc = pthread_mutex_destroy(&conn->flush_mutex);
    if (rc)
      debug(1, "Connection %d: error %d destroying flush_mutex.", conn->connection_number, rc);
    debug(3, "Connection %d: Closed.", conn->connection_number);
    sessions.markFinished(conn->connection_number);
    pthread_setcancelstate(oldState, NULL);
  }
}

void msg_cleanup_function(void *arg) {
  debug(4, "msg_cleanup_function called 0x%" PRIxPTR ".", (uintptr_t)arg);
  msg_free((RtspMessage **)arg);
}

static void *rtsp_conversation_thread_func(void *pconn) {
  //  #include <syscall.h>
  //  debug(1, "rtsp_conversation_thread_func PID %d", syscall(SYS_gettid));
  rtsp_conn_info *conn = static_cast<rtsp_conn_info *>(pconn);

  int rc = pthread_mutex_init(&conn->flush_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising flush_mutex.", conn->connection_number, rc);

  // nothing before this is cancellable
  pthread_cleanup_push(rtsp_conversation_thread_cleanup_function, (void *)conn);

  rtp_initialise(conn);
  const char *hdr = NULL;

  enum rtsp_read_request_response reply;

  // int rtsp_read_request_attempt_count = 1; // 1 means exit immediately
  RtspMessage *req = NULL, *resp = NULL;

  conn->ap2_audio_buffer_size = 1024 * 1024 * 8;

  while (conn->stop == 0) {
    pthread_testcancel();
    int debug_level = 4; // for printing the request and response

    reply = rtsp_read_request(conn, &req);
    if (reply == rtsp_read_request_response_ok) {
      pthread_cleanup_push(msg_cleanup_function, (void *)&req);
      resp = msg_init();
      pthread_cleanup_push(msg_cleanup_function, (void *)&resp);
      resp->respondWith(501); // Not Implemented
      int dl = debug_level;
      // if ((strcmp(req->methodName(), "OPTIONS") == 0) ||
      //    (strcmp(req->methodName(), "POST") ==
      //     0)) // the options message is very common, so don't log it until level 3
      //  dl = 3;
      debug(dl,
            "Connection %d: (%s) received an RTSP Packet of type \"%s\":", conn->connection_number,
            get_category_string(conn->airplay_stream_category), req->methodName());
      debug_log_rtsp_message(dl, NULL, req);

      hdr = req->headerValue("CSeq");
      if (hdr)
        resp->addHeader("CSeq", hdr);
      //      resp->addHeader("Audio-Jack-Status", "connected; type=analog");
      char server_string[128];
      snprintf(server_string, sizeof(server_string), "AirTunes/%s", config.srcvers);
      resp->addHeader("Server", server_string);

      rtsp_dispatch_request(conn, req, resp);
      debug(dl, "Connection %d: (%s) RTSP response:", conn->connection_number,
            get_category_string(conn->airplay_stream_category));
      debug_log_rtsp_message(dl, NULL, resp);
      // if (conn->stop == 0) {
      int err = msg_write_response(conn, resp);
      if (err) {
        debug(1,
              "Connection %d: Unable to write an RTSP message response. Terminating the "
              "connection.",
              conn->connection_number);
        struct linger so_linger;
        so_linger.l_onoff = 1; // "true"
        so_linger.l_linger = 0;
        err = setsockopt(conn->fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
        if (err)
          debug(1,
                "Connection %d: Could not set the RTSP socket to abort due to a write error on "
                "closing.",
                conn->connection_number);
        conn->stop = 1;
        // if (debuglev >= 1)
        //  debuglev = 3; // see what happens next
      }
      // }
      pthread_cleanup_pop(1);
      pthread_cleanup_pop(1);
    } else { // if the response is not rtsp_read_request_response_ok
      conn->stop = 1;
      if (reply == rtsp_read_request_response_read_error) {
        debug(1, "bad packet received.");
        struct linger so_linger;
        so_linger.l_onoff = 1; // "true"
        so_linger.l_linger = 0;
        int err = setsockopt(conn->fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
        if (err)
          debug(1, "Could not set the RTSP socket to abort due to a read error on closing.");
      } else if (reply == rtsp_read_request_response_bad_packet) {
        conn->stop = 0; // don't stop for a bad packet
        const char *response_text = "RTSP/1.0 400 Bad Request\r\nServer: AirTunes/105.1\r\n\r\n";
        ssize_t lreply = write(conn->fd, response_text, strlen(response_text));
        if (lreply == -1) {
          char errorstring[1024];
          strerror_r(errno, (char *)errorstring, sizeof(errorstring));
          debug(1, "rtsp_read_request_response_bad_packet write response error %d: \"%s\".", errno,
                (char *)errorstring);
        } else if (lreply != (ssize_t)strlen(response_text)) {
          debug(1,
                "rtsp_read_request_response_bad_packet write %zd bytes requested but %d written.",
                strlen(response_text), reply);
        }
      }
    }
  }
  release_play_lock(conn);
  pthread_cleanup_pop(1);
  debug(2, "Connection %d: exit.", conn->connection_number);
  pthread_exit(NULL);
}

/*
// this function is not thread safe.
static const char *format_address(struct sockaddr *fsa) {
  static char string[INETx_ADDRSTRLEN];
  void *addr;
#ifdef AF_INET6
  if (fsa->sa_family == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)(fsa);
    addr = &(sa6->sin6_addr);
  } else
#endif
  {
    struct sockaddr_in *sa = (struct sockaddr_in *)(fsa);
    addr = &(sa->sin_addr);
  }
  return inet_ntop(fsa->sa_family, addr, string, sizeof(string));
}
*/

static void rtsp_listen_loop(RtspListener &listener, std::stop_token stop,
                             const std::shared_ptr<SharedRtpTimingSettings> &timing) {
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
  pthread_setname_np(pthread_self(), "listener");
  struct addrinfo hints, *info, *p;
  char portstr[6];
  int ret;

  principalSession.clear();

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  snprintf(portstr, 6, "%d", config.port);

  // debug(1,"listen socket port request is \"%s\".",portstr);

  ret = getaddrinfo(NULL, portstr, &hints, &info);
  if (ret) {
    warn("getaddrinfo failed: %s", gai_strerror(ret));
    exit_request(EXIT_FAILURE);
    return;
  }

  const auto addresses = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>(info, freeaddrinfo);
  for (p = addresses.get(); p; p = p->ai_next) {
    ret = 0;
    int lfd = socket(p->ai_family, p->ai_socktype, IPPROTO_TCP);
    int yes = 1;

    // Handle socket open failures if protocol unavailable (or IPV6 not handled)
    if (lfd != -1) {
      // Set the RTSP socket to close on exec() of child processes
      // otherwise background run_this_before_play_begins or run_this_after_play_ends commands
      // that are sleeping prevent the daemon from being restarted because
      // the listening RTSP port is still in use.
      // See: https://github.com/mikebrady/shairport-sync/issues/329
      fcntl(lfd, F_SETFD, FD_CLOEXEC);
      ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef IPV6_V6ONLY
      // some systems don't support v4 access on v6 sockets, but some do.
      // since we need to account for two sockets we might as well
      // always.
      if (p->ai_family == AF_INET6) {
        ret |= setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
      }
#endif

      if (!ret)
        ret = bind(lfd, p->ai_addr, p->ai_addrlen);

      // one of the address families will fail on some systems that
      // report its availability. do not complain.

      if (!ret)
        ret = listen(lfd, 255);

      if (ret) {
        const char *family;
#ifdef AF_INET6
        if (p->ai_family == AF_INET6) {
          family = "IPv6";
        } else
#endif
          family = "IPv4";
        debug(1, "unable to listen on %s port %d. The error is: \"%s\".", family, config.port,
              strerror(errno));
        close(lfd);
      } else {
        listener.addSocket(lfd);
      }
    }

  }

  if (listener.hasSockets() && !stop.stop_requested()) {
    const char **t1 = txt_records; // ap1 text records
    const char **t2 = NULL;        // possibly two text records

      // make up a secondary set of text records
      t2 = secondary_txt_records; // second set of text records in AirPlay 2 only

    build_bonjour_strings(NULL); // no conn yet
    // if a thread is created, e.g. Avahi, it'll inherit the name from this thread
    mdns_register(t1, t2); // note that the dacp thread could still be using the mdns stuff after
                           // all player threads have been terminated, so mdns_unregister can't be
                           // in the rtsp_listen_loop cleanup.
    while (!stop.stop_requested()) {
      cleanup_threads();
      SOCKADDR remote;
      socklen_t size_of_reply = sizeof(remote);
      int acceptedSocket = listener.accept(stop, (struct sockaddr *)&remote, &size_of_reply);
      if (acceptedSocket < 0) {
        continue;
      }

      std::unique_ptr<SessionState> unregistered;
      try {
        unregistered = SessionState::create(timing);
      } catch (const std::bad_alloc &) {
        close(acceptedSocket);
        warn("Couldn't allocate memory for an rtsp_conn_info record.");
        exit_request(EXIT_FAILURE);
        break;
      }
      auto *conn = unregistered.get();
      conn->fd = acceptedSocket;
      conn->remote = remote;
      conn->connection_number = RTSP_connection_index++;
      debug(2, "Connection %d is at: 0x%" PRIxPTR ".", conn->connection_number, (uintptr_t)conn);

      // this means that the OPTIONS string we send before getting an ANNOUNCE is for AirPlay 2

      {
        size_of_reply = sizeof(SOCKADDR);
        if (getsockname(conn->fd, (struct sockaddr *)&conn->local, &size_of_reply) == 0) {

          if ((config.dont_check_timeout == 0) && (config.timeout >= 60)) {
            /*
              // shouldn't need this!

              struct timeval tv;
              tv.tv_sec = config.timeout;           // seconds
              tv.tv_usec = 0; // microseconds
              if (setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) != 0)
              { char errorstring[1024]; strerror_r(errno, (char *)errorstring, sizeof(errorstring));
                debug(1, "could not set time limit on read_from_rtsp_connection -- error %d
              \"%s\".", errno, errorstring);
              }
            */
// skip this stuff in OpenBSD
            // Thanks to https://holmeshe.me/network-essentials-setsockopt-SO_KEEPALIVE/ for this.

            // turn on keepalive stuff -- wait for keepidle + (keepcnt * keepinttvl time) seconds
            // before giving up an ETIMEOUT error is returned if the keepalive check fails

            // if TCP_KEEPINTVL is defined, check a few times before declaring the line dead
            // otherwise just wait a little while longer

#ifdef TCP_KEEPINTVL
            int keepAliveIdleTime =
                config.timeout -
                5 * 5; // wait this many seconds before checking for a dropped client
            // a minute seems a bit short...
            int keepAliveCount = 5;    // check this many times
            int keepAliveInterval = 5; // wait this many seconds between checks
#else
            int keepAliveIdleTime =
                config.timeout; // wait this many seconds before dropping a client
#endif

// --- the following is a bit  too complicated
// decide to use IPPROTO_TCP or SOL_TCP
#define SOL_OPTION SOL_TCP
// decide to use TCP_KEEPALIVE or TCP_KEEPIDLE
#define KEEP_ALIVE_OR_IDLE_OPTION TCP_KEEPIDLE
            debug(3, "Connection %d: set the keepAliveIdleTime to %d seconds.",
                  conn->connection_number, keepAliveIdleTime);
            if (setsockopt(conn->fd, SOL_OPTION, KEEP_ALIVE_OR_IDLE_OPTION,
                           (void *)&keepAliveIdleTime, sizeof(keepAliveIdleTime))) {
              debug(1, "can't set the keepAliveIdleTime wait time");
            }
// ---
// if TCP_KEEPINTVL is defined...
#ifdef TCP_KEEPINTVL
            debug(3, "Connection %d: set the keepAliveCount to %d.", conn->connection_number,
                  keepAliveCount);
            if (setsockopt(conn->fd, SOL_OPTION, TCP_KEEPCNT, (void *)&keepAliveCount,
                           sizeof(keepAliveCount))) {
              debug(1, "can't set the keepAliveCount count");
            }
            debug(3, "Connection %d: set the keepAliveCount interval to %d seconds.",
                  conn->connection_number, keepAliveInterval);
            if (setsockopt(conn->fd, SOL_OPTION, TCP_KEEPINTVL, (void *)&keepAliveInterval,
                           sizeof(keepAliveInterval))) {
              debug(1, "can't set the keepAliveCount count interval");
            };
#endif
            debug(3, "Connection %d: enable SO_KEEPALIVE.", conn->connection_number);
            int flags = 1;
            if (setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags))) {
              debug(1, "can't set SO_KEEPALIVE.");
            }
          }

          // initialise the connection info
          void *client_addr = NULL, *self_addr = NULL;
          conn->connection_ip_family = conn->local.SAFAMILY;

#ifdef AF_INET6
          if (conn->connection_ip_family == AF_INET6) {
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&conn->remote;
            client_addr = &(sa6->sin6_addr);
            conn->client_rtsp_port = ntohs(sa6->sin6_port);

            sa6 = (struct sockaddr_in6 *)&conn->local;
            self_addr = &(sa6->sin6_addr);
            conn->self_rtsp_port = ntohs(sa6->sin6_port);
            conn->self_scope_id = sa6->sin6_scope_id;
          }
#endif
          if (conn->connection_ip_family == AF_INET) {
            struct sockaddr_in *sa4 = (struct sockaddr_in *)&conn->remote;
            client_addr = &(sa4->sin_addr);
            conn->client_rtsp_port = ntohs(sa4->sin_port);

            sa4 = (struct sockaddr_in *)&conn->local;
            self_addr = &(sa4->sin_addr);
            conn->self_rtsp_port = ntohs(sa4->sin_port);
          }

          inet_ntop(conn->connection_ip_family, client_addr, conn->client_ip_string,
                    sizeof(conn->client_ip_string));
          inet_ntop(conn->connection_ip_family, self_addr, conn->self_ip_string,
                    sizeof(conn->self_ip_string));

          debug(2, "Connection %d: New connection from %s:%u to self at %s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                conn->self_ip_string, conn->self_rtsp_port);
          conn->connection_start_time = get_absolute_time_in_ns();
        } else {
          debug(1, "Error figuring out Shairport Sync's own IP number.");
        }

        const int connectionNumber = conn->connection_number;
        if (stop.stop_requested())
          break;
        int previousState;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
        auto owner = std::move(unregistered);
        conn = nullptr;
        ret = sessions.start(std::make_unique<RuntimeSessionWorker>(
            std::move(owner), rtsp_conversation_thread_func));
        pthread_setcancelstate(previousState, nullptr);
        if (ret) {
          char errorstring[1024];
          strerror_r(ret, (char *)errorstring, sizeof(errorstring));
          warn("Connection %d: cannot create an RTSP conversation thread. Error %d: \"%s\".",
              connectionNumber, ret, (char *)errorstring);
          exit_request(EXIT_FAILURE);
          break;
        }


        if (ret == 0)
          debug(3, "Successfully created RTSP receiver thread %d.", connectionNumber);
      }
    }
  } else if (!stop.stop_requested()) {
    warn("could not establish a service on port %d -- program terminating. Is another instance of "
        "Shairport Sync running on this device?",
        config.port);
    exit_request(EXIT_FAILURE);
  }
}

static RtspListener &rtspListener() {
  static RtspListener listener;
  return listener;
}

int rtsp_listener_start(std::shared_ptr<SharedRtpTimingSettings> timing) {
  if (!timing)
    return EINVAL;
  try {
    auto &listener = rtspListener();
    atexit(rtsp_listener_stop);
    listener.start([&listener, timing = std::move(timing)](std::stop_token stop) {
      try {
        rtsp_listen_loop(listener, stop, timing);
      } catch (const std::exception &error) {
        warn("RTSP listener failed: %s", error.what());
        exit_request(EXIT_FAILURE);
      }
      sessions.shutdown();
    });
    return 0;
  } catch (const std::system_error &error) {
    return error.code().value();
  }
}

void rtsp_listener_stop() {
  rtspListener().stop();
}

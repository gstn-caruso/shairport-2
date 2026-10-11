#pragma once

#include "session/session_category.h"
#include "session/principal_participant.hpp"
#include "session/pairing_session.hpp"
#include "protocol/ap2/pairing_directory_port.hpp"
#include "protocol/ap2/buffered_flush_policy.hpp"
#include "runtime/common.h"
#include "playback/player.h"
#include "timing/rtp_clock.hpp"
#include "timing/shared_rtp_timing_settings.hpp"
#include "protocol/rtp/resend_coordinator.hpp"
#include "transport/remote_control_peer.hpp"
#include "transport/control_datagram_channel.hpp"
#include "protocol/ap2/event_command_exchange.hpp"
#include "transport/event_port_worker.hpp"
#include "audio/decoding/audio_decoder.hpp"
#include "audio/format/audio_input_state.hpp"
#include "audio/resampling/resampler.hpp"
#include "audio/buffer/audio_packet_buffer.hpp"
#include "audio/pcm/pcm_encoder.hpp"
#include "audio/pcm/playback_samples.hpp"
#include "playback/timing/playback_sync.hpp"
#include "playback/playback_statistics.hpp"
#include "volume/volume_control.hpp"
#include "playback/timing/playback_timing.hpp"
#include "playback/playback_run.hpp"
#include <cstdlib>
#include <atomic>

struct SessionState : PrincipalParticipant {
  SessionState() = default;
  static std::unique_ptr<SessionState> create(std::shared_ptr<SharedRtpTimingSettings> timing,
                                            std::shared_ptr<PairingDirectoryPort> directory,
                                            std::shared_ptr<const PairingOptions> options);
  SharedRtpTimingSettings &timingSettings() const { return *timingSettings_; }
  PairingDirectoryPort &pairingDirectory() const { return *pairingDirectory_; }
  SessionState(const SessionState &) = delete;
  SessionState &operator=(const SessionState &) = delete;
  ~SessionState() override;
  int id() const override { return connection_number; }
  bool mayAcquirePrincipal() const override { return !retiring_.load(); }
  void beginRetirement() override { retiring_.store(true); }
  int connection_number;           // for debug ID purposes, nothing else...
  int resend_interval;                      // this is really just for debugging
  char *UserAgent;                          // free this on teardown
  int AirPlayVersion; // zero if not an AirPlay session. Used to help calculate latency
  int latency_warning_issued;
  uint32_t latency;          // the actual latency used for this play session
  uint32_t minimum_latency;  // set if an a=min-latency: line appears in the ANNOUNCE message; zero
                             // otherwise
  uint32_t maximum_latency;  // set if an a=max-latency: line appears in the ANNOUNCE message; zero
                             // otherwise
  int fd = -1;
  SOCKADDR remote, local;
  volatile int stop;

  uint64_t connection_start_time; // the time the device is selected, which could be a long time
                                  // before a play
  pthread_t thread;

  PlaybackStatistics statistics;
  PlaybackRun playbackRun;
  AudioPacketBuffer packetBuffer;
  AudioInputState inputAudio;
  // int connection_state_to_output;
  PlaybackTiming playbackTiming;
  // debug variables
  int last_seqno_valid;
  seq_t last_seqno_read;
  // mutexes and condition variables
  pthread_mutex_t flush_mutex;
  VolumeControl volumeControl;






  // RTP stuff
  // only one RTP session can be active at a time.
  ControlDatagramChannel controlChannel;
  ResendCoordinator resendCoordinator;

  char client_ip_string[INET6_ADDRSTRLEN]; // the ip string of the client
  uint16_t client_rtsp_port;
  char self_ip_string[INET6_ADDRSTRLEN]; // the ip string being used by this program -- it
  uint16_t self_rtsp_port;               // could be one of many, so we need to know it

  uint32_t self_scope_id;     // if it's an ipv6 connection, this will be its scope
  short connection_ip_family; // AF_INET / AF_INET6


  int64_t latency_delayed_timestamp; // this is for debugging only...

  // this is what connects an rtp timestamp to the remote time

  int udp_clock_sender_is_initialised;

  RtpClock clock;

  std::atomic<airplay_stream_c> airplay_stream_category{unspecified_stream_category};

  plist_t sessionPlist;
  char *airplay_gid; // UUID in the Bonjour advertisement -- if NULL, the group UUID is the same as
                     // the pi UUID
  airplay_stream_t airplay_stream_type; // is it realtime audio or buffered audio...

  pthread_t *rtp_data_thread;
  pthread_t rtp_ap2_control_thread;
  pthread_t rtp_realtime_audio_thread;
  pthread_t rtp_buffered_audio_thread;
  EventCommandExchange eventCommandExchange;
  EventPortWorker eventPortWorker;

  BufferedFlushPolicy bufferedFlush;

  ssize_t ap2_audio_buffer_size;

  int ap2_rate;         // protect with flush mutex, 0 means don't play, 1 means play
  int ap2_play_enabled; // protect with flush mutex

  PairingSession pairingSession;

  int data_socket;
  RemoteControlPeer remoteControlPeer;
  int realtime_audio_socket;
  int buffered_audio_socket;

  uint16_t local_data_port;
  uint16_t local_event_port;
  uint16_t local_ap2_control_port;
  uint16_t local_realtime_audio_port;
  uint16_t local_buffered_audio_port;

  uint64_t audio_format;
  uint64_t compression;
  unsigned char *session_key; // needs to be free'd at the end
  char *ap2_client_name;      // needs to be free'd at teardown phase 2
  uint64_t frames_packet;
  uint64_t type;                  // 96 (Realtime Audio), 103 (Buffered Audio), 130 (Remote Control)
  uint64_t networkTimeTimelineID; // the clock ID used by the player
  uint8_t groupContainsGroupLeader; // information coming from the SETUP
  uint64_t compressionType;

  AudioDecoder decoder;
  Resampler resampler;
  PcmEncoder pcmEncoder{[] {
    r64_lock;
    const auto random = r64i();
    r64_unlock;
    return random;
  }};
  PlaybackSamples playbackSamples{[](size_t frames) {
    return (std::rand() % (frames - 2)) + 1;
  }};
  PlaybackSync playbackSync;

  // used as the initials values for calculating the rate at which the source thinks it's sending
  // frames
  uint32_t initial_reference_timestamp;
  uint64_t initial_reference_time;

  // the ratio of the following should give us the operating rate, nominally 44,100
  int64_t reference_to_previous_frame_difference;
  uint64_t reference_to_previous_time_difference;

  // debug variables
  int request_sent;

  pthread_mutex_t reference_time_mutex;


  // int64_t play_segment_reference_frame;
  // uint64_t play_segment_reference_frame_remote_time;



  // remote control stuff. The port to which to send commands is not specified, so you have to use
  // mdns to find it.
  // at present, only avahi can do this

  char *dacp_id; // id of the client -- used to find the port to be used
  //  uint16_t dacp_port;          // port on the client to send remote control messages to, else
  //  zero
  char *dacp_active_remote;   // key to send to the remote controller
  void *dapo_private_storage; // this is used for compatibility, if dacp stuff isn't enabled.

private:
  std::shared_ptr<PairingDirectoryPort> pairingDirectory_;
  std::shared_ptr<SharedRtpTimingSettings> timingSettings_ =
      std::make_shared<SharedRtpTimingSettings>(0);
  std::atomic<bool> retiring_{false};
};

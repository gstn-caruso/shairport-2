#ifndef _COMMON_H
#define _COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h> // for mode_t
#include <unistd.h>    // for useconds_t

#include "config.h"
#include "runtime/definitions.h"
#include "app/settings_types.h"
#include "discovery/mdns.h"

#ifdef __cplusplus
extern "C" {
#endif


// struct sockaddr_in6 is bigger than struct sockaddr. derp
#ifdef AF_INET6
#define SOCKADDR struct sockaddr_storage
#define SAFAMILY ss_family
#else
#define SOCKADDR struct sockaddr
#define SAFAMILY sa_family
#endif



#define sps_extra_code_output_stalled 32768
#define sps_extra_code_output_state_cannot_make_ready 32769

// yeah/no/auto
typedef enum { YNA_AUTO = -1, YNA_NO = 0, YNA_YES = 1 } yna_type;

// yeah/no/dont-care
typedef enum { YNDK_DONT_KNOW = -1, YNDK_NO = 0, YNDK_YES = 1 } yndk_type;









typedef enum {
  decoder_ffmpeg_alac,
} decoders_supported_type;

typedef enum {
  disable_standby_off = 0,
  disable_standby_auto,
  disable_standby_always
} disable_standby_mode_type;

// the following enum is for the formats recognised -- currently only S16LE is recognised for input,
// so these are output only for the present

// ensure sps_format_sample_size_array and sps_format_description_string_array are in sync with
// this!
#include "audio/format/audio_types.h"



#include "audio/format/encoded_output_format.h"

const char *short_format_description(int32_t encoded_format);
const char *sps_format_description_string(sps_format_t format);

unsigned int sps_format_sample_size(sps_format_t format);
unsigned int sps_rate_actual_rate(sps_rate_t rate);

typedef struct {
  double missing_port_dacp_scan_interval_seconds; // if no DACP port number can be found, check at
                                                  // these intervals
  double resend_control_first_check_time; // wait this long before asking for a missing packet to be
                                          // resent
  double resend_control_check_interval_time; // wait this long between making requests
  double resend_control_last_check_time; // if the packet is missing this close to the time of use,
                                         // give up

  pthread_mutex_t lock;
  config_t *cfg;
  int endianness;
  double default_airplay_volume;
  char *appName; // normally the app is called shairport-syn, but it may be symlinked
  char *password;
  char *service_name; // the name for the shairport service, e.g. "Shairport Sync Version %v running
                      // on host %h"

  char *pa_server;           // the pulseaudio server address that Shairport Sync will play on.
  char *pa_application_name; // the name under which Shairport Sync shows up as an "Application" in
                             // the Sound Preferences in most desktop Linuxes.
  // Defaults to "Shairport Sync". Shairport Sync must be playing to see it.

  char *pa_sink; // the name (or id) of the sink that Shairport Sync will play on.




  uint8_t ap1_prefix[6];
  uint8_t hw_addr[8]; // only needs 6 but 8 is handy when converting this to a number
  int port;
  int udp_port_base;
  int udp_port_range;
  int ignore_volume_control;
  int volume_max_db_set; // set to 1 if a maximum volume db has been set
  int volume_max_db;
  int no_sync;             // disable synchronisation, even if it's available
  int no_mmap;             // disable use of mmap-based output, even if it's available
  double resync_threshold; // if it gets out of whack by more than this number of seconds, do a
                           // resync. if zero, never do a resync.
  int allow_session_interruption;
  int timeout; // while in play mode, exit if no packets of audio come in for more than this number
               // of seconds . Zero means never exit.
  int dont_check_timeout; // this is used to maintain backward compatibility with the old -t option
                          // behaviour; only set by -t 0, cleared by everything else
  char *output_name;
  audio_output *output;
  mdns_backend *mdns;
  int buffer_start_fill;
  uint32_t userSuppliedLatency; // overrides all other latencies -- use with caution
  uint32_t fixedLatencyOffset;  // add this to all automatic latencies supplied to get the actual
                                // total latency
// the total latency will be limited to the min and max-latency values, if supplied

  int log_fd;                      // file descriptor of the file or pipe to log stuff to.
  char *log_file_path;             // path to file or pipe to log to, if any
  int logOutputLevel;              // log output level
  int debugger_show_elapsed_time;  // in the debug message, display the time since startup
  int debugger_show_relative_time; // in the debug message, display the time since the last one
  int debugger_show_file_and_line; // in the debug message, display the filename and line number
  int statistics_requested;
  playback_mode_type playback_mode;
  char *cmd_start, *cmd_stop, *cmd_set_volume, *cmd_unfixable;
  char *cmd_active_start, *cmd_active_stop;
  int cmd_blocking, cmd_start_returns_output;
  double tolerance; // allow this much drift before attempting to correct it
  stuffing_type packet_stuffing;
                            // to be enabled under the auto setting
  int decoder_in_use;
  char *configfile;
  char *regtype; // Complementary AirPlay 2 discovery service: "_raop._tcp".
  char *regtype2; // Primary AirPlay 2 discovery service: "_airplay._tcp".
  char *interface; // a string containg the interface name, or NULL if nothing specified
  int interface_index;                        // only valid if the interface string is non-NULL
  double audio_backend_buffer_desired_length; // this will be the length in seconds of the
                                              // audio backend buffer -- the DAC buffer for ALSA
  double audio_backend_buffer_interpolation_threshold_in_seconds; // below this, soxr interpolation
                                                                  // will not occur -- it'll be
                                                                  // basic interpolation instead.
  double audio_decoded_buffer_desired_length;    // the length of the buffer of fully decoded audio
                                                 // prior to being sent to the output device
  double disable_standby_mode_silence_threshold; // below this, silence will be added to the output
                                                 // buffer
  double disable_standby_mode_silence_scan_interval; // check the threshold this often

  int audio_backend_silent_lead_in_time_auto; // true if the lead-in time should be from as soon as
                                              // packets are received
  double audio_backend_silent_lead_in_time; // the length of the silence that should precede a play.
  uint32_t minimum_free_buffer_headroom; // when effective latency is calculated, ensure this number
                                         // of buffers are unallocated
  double active_state_timeout; // the amount of time from when play ends to when the system leaves
                               // into the "active" mode.
  uint32_t volume_range_db; // the range, in dB, from max dB to min dB. Zero means use the mixer's
                            // native range.
  int volume_range_hw_priority; // when extending the volume range by combining sw and hw
                                // attenuators, lowering the volume, use all the hw attenuation
                                // before using
                                // sw attenuation
  volume_control_profile_type volume_control_profile;
  int output_format_auto_requested; // true if the configuration requests auto configuration
  int output_rate_auto_requested;   // true if the configuration requests auto configuration
  uint32_t current_output_configuration;
  // these are the formats/rate and channel configurations permitted by the settings or defaults
  uint32_t format_set;
  uint32_t rate_set;
  uint32_t channel_set;


  disable_standby_mode_type disable_standby_mode;
  volatile int keep_dac_busy;
  yna_type use_precision_timing; // defaults to no


  int disable_resend_requests; // set this to stop resend request being made for missing packets
  double diagnostic_drop_packet_fraction; // pseudo randomly drop this fraction of packets, for
                                          // debugging. Currently audio packets only...

                   // can't use IP numbers as they might be given to different devices
                   // can't get hold of MAC addresses.
                   // can't define the null linked list struct here
  char *firmware_version;
  // use these in information requests
  char *model;
  char *srcvers;
  char *osvers;

  uint64_t airplay_features;
  uint32_t airplay_statusflags;
  char *airplay_fex;       // a base64-encoded version of the airplay_features in little-endian form
  char *airplay_device_id; // for the Bonjour advertisement and the GETINFO PList
  char *airplay_pi;        // UUID in the Bonjour advertisement and the GETINFO Plist
  char *airplay_pgid;      // UUID in the txtAirPlay data sent on the event channel
  char *airplay_psi;       // type 4 fixed UUID
  uint8_t airplay_pk[32];  // public key
  char *pk_string;
  char *nqptp_shared_memory_interface_name; // client name for nqptp service
  int enable_HK_Access_Control;             // true if the device is part of an Apple Home


  int unfixable_error_reported; // only report once.

  uint64_t eight_channel_layout; // non-zero means enabled and is a channel layout
  uint64_t six_channel_layout;   // non-zero means enabled and is a channel layout

  int mixdown_enable;
  uint64_t mixdown_channel_layout;   // if mixdown_enable is true, 0 signifies auto, based on number
                                     // of channels
  int output_channel_mapping_enable; // 0 means off, non-zero means on. If on and
                                     // output_channel_map_size is 0, use the device's channel map
  const char *output_channel_map[8]; // names of the output channels
  unsigned int output_channel_map_size; // number of output channels


} shairport_cfg;

uint32_t nctohl(const uint8_t *p);  // read 4 characters from *p and do ntohl on them
uint16_t nctohs(const uint8_t *p);  // read 2 characters from *p and do ntohs on them
uint64_t nctoh64(const uint8_t *p); // read 8 characters from *p to a uint64_t

int try_to_open_pipe_for_writing(
    const char *pathname); // open it without blocking if it's not hooked up

// based on http://burtleburtle.net/bob/rand/smallprng.html

void r64init(uint64_t seed);
uint64_t r64u();
int64_t r64i();

// if you are breaking in to a session, you need to avoid the ports of the current session
// if you are law-abiding, then you can reuse the ports.
// so, you can reset the free UDP ports minder when you're legit, and leave it otherwise

// the downside of using different ports each time is that it might make the firewall
// rules a bit more complex, as they need to allow more than the minimum three ports.
// a range of 10 is suggested anyway

void resetFreeUDPPort();
uint16_t nextFreeUDPPort();

extern volatile int debuglev;

// Thanks to https://stackoverflow.com/a/1597129 for the inspiration for this identifier generation
#define MAKEUNIQUEID2(x, y) x##y
#define MADEID(x, y) MAKEUNIQUEID2(x, y)

// do X once, and never again until the app is restarted
#define once(X)                                                                                    \
  static int MADEID(once_flag_, __LINE__) = 0;                                                     \
  if (MADEID(once_flag_, __LINE__) == 0) {                                                         \
    X;                                                                                             \
    MADEID(once_flag_, __LINE__) = 1;                                                              \
  }

void getErrorText(char *destinationString, size_t destinationStringLength);

uint8_t *base64_dec(char *input, int *outlen);
char *base64_enc(uint8_t *input, int length);

char *base64_encode_so(const unsigned char *data, size_t input_length, char *encoded_data,
                       size_t *output_length);

// return a time in nanoseconds
// Not defined for macOS
uint64_t get_realtime_in_ns(void);
uint64_t get_absolute_time_in_ns(void);  // monotonic_raw or monotonic
uint64_t get_monotonic_time_in_ns(void); // NTP-disciplined

// time at startup for debugging timing
// extern uint64_t ns_time_at_startup, ns_time_at_last_debug_message;

// this is for reading an unsigned 32 bit number, such as an RTP timestamp

uint32_t uatoi(const char *nptr);

extern shairport_cfg config;


int check_string_or_list_setting(config_setting_t *setting, const char *item);
int check_int_or_list_setting(config_setting_t *setting, const int item);



void command_start(void);
void command_stop(void);
void command_execute(const char *command, const char *extra_argument, const int block);
void command_set_volume(double volume);

int mkpath(const char *path, mode_t mode);

#define pthread_mutex_lock_and_cleanup_push(mu)                                                 \
  if (pthread_mutex_lock(mu) == 0)                                   \
  pthread_cleanup_push(mutex_unlock, (void *)mu)



int do_pthread_setname(pthread_t *thread, const char *format, ...);

int named_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg, const char *format,
                         ...);
int named_pthread_create_with_priority(pthread_t *thread, int priority,
                                       void *(*start_routine)(void *), void *arg,
                                       const char *format, ...);

extern pthread_mutex_t r64_mutex;

#define r64_lock pthread_mutex_lock(&r64_mutex)

#define r64_unlock pthread_mutex_unlock(&r64_mutex)

char *get_version_string(); // mallocs a string space -- remember to free it afterwards



int string_update_with_size(char **str, int *flag, char *s, size_t len);

// from https://stackoverflow.com/questions/13663617/memdup-function-in-c, with thanks
void *memdup(const void *mem, size_t size);

int bind_socket_and_port(int type, int ip_family, const char *self_ip_address, uint32_t scope_id,
                         uint16_t *port, int *sock);

uint16_t bind_UDP_port(int ip_family, const char *self_ip_address, uint32_t scope_id, int *sock);

// for pthread_push and pop
// careful with the difference between cleanup and unlock!

void malloc_cleanup(void *arg);
void socket_cleanup(void *arg);
void mutex_unlock(void *arg);
void mutex_cleanup(void *arg);
void rwlock_unlock(void *arg);
void cv_cleanup(void *arg);
void thread_cleanup(void *arg);
void plist_cleanup(void *arg);

char *debug_malloc_hex_cstring(void *packet, size_t nread);

// from https://stackoverflow.com/questions/13663617/memdup-function-in-c, with thanks
// allocates memory and copies the content to it
// analogous to strndup;
void *memdup(const void *mem, size_t size);

int get_device_id(uint8_t *id, int int_length);

char *bnprintf(char *buffer, ssize_t max_bytes, const char *format, ...);


#ifdef CONFIG_USE_GIT_VERSION_STRING
extern char git_version_string[];
#endif

#ifdef __cplusplus
}
#endif

#endif // _COMMON_H

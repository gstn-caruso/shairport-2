#ifndef _PLAYER_H
#define _PLAYER_H

#include <arpa/inet.h>
#include <pthread.h>

#include "config.h"
#include "runtime/definitions.h"
#include "timing/clock_status.h"



#include "platform/utilities/pairing_api.h"
#include <plist/plist.h>

#include "platform/utilities/ffmpeg_api.h"

#include "audio/output/audio.h"



// these are for reporting the status of the clock
typedef uint16_t seq_t;

// these are the values coming in on a buffered audio RTP packet's SSRC field
// the apparent significances are as indicated.
// Dolby Atmos seems to be 7P1
#include "audio/format/audio_types.h"


// maximum number of frames that can be added or removed from a packet_count
#include "packets/packet_limits.h"
#define INTERPOLATION_LIMIT 20


#include "session/session_category.h"

typedef enum { realtime_stream, buffered_stream } airplay_stream_t;

#ifdef __cplusplus
struct SessionState;
using rtsp_conn_info = SessionState;
#else
typedef struct rtsp_conn_info rtsp_conn_info;
#endif


#ifdef __cplusplus
extern "C" {
#endif


void reset_buffer(rtsp_conn_info *conn);

size_t get_audio_buffer_occupancy(rtsp_conn_info *conn);

int32_t modulo_32_offset(uint32_t from, uint32_t to);

void ab_resync(rtsp_conn_info *conn);

int player_play(rtsp_conn_info *conn);
int player_stop(rtsp_conn_info *conn);

void player_volume(double f, rtsp_conn_info *conn);
void player_volume_without_notification(double f, rtsp_conn_info *conn);
void player_flush(uint32_t timestamp, rtsp_conn_info *conn);
// void player_full_flush(rtsp_conn_info *conn);

seq_t get_revised_seqno(rtsp_conn_info *conn, uint32_t timestamp);
void clear_buffers_from(rtsp_conn_info *conn, seq_t from_here);
uint32_t player_put_packet(uint32_t ssrc, seq_t seqno, uint32_t actual_timestamp, uint8_t *data,
                           size_t len, int mute, int32_t timestamp_gap, rtsp_conn_info *conn);
int64_t monotonic_timestamp(uint32_t timestamp,
                            rtsp_conn_info *conn); // add an epoch to the timestamp. The monotonic
// timestamp guaranteed to start between 2^32 2^33
// frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

double suggested_volume(rtsp_conn_info *conn); // volume suggested for the connection

const char *get_ssrc_name(ssrc_t ssrc);
size_t get_ssrc_block_length(ssrc_t ssrc);

const char *get_category_string(airplay_stream_c cat);

int ssrc_is_recognised(ssrc_t ssrc);
int ssrc_is_aac(ssrc_t ssrc); // used to decide if a mute might be needed (AAC only)
void prepare_decoding_chain(rtsp_conn_info *conn, ssrc_t ssrc); // also sets up timing stuff
void clear_decoding_chain(rtsp_conn_info *conn);                // tear down the decoding chain
AVFrame *block_to_avframe(rtsp_conn_info *conn, uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif //_PLAYER_H

#ifndef _RTSP_H
#define _RTSP_H

#include "playback/player.h"

#ifdef __cplusplus
#include <memory>
class SharedRtpTimingSettings;
class PairingDirectoryPort;
struct PairingOptions;
int rtsp_listener_start(std::shared_ptr<SharedRtpTimingSettings> timing,
                        std::shared_ptr<PairingDirectoryPort> directory,
                        std::shared_ptr<const PairingOptions> options);
class RtspMessage;
using rtsp_message = RtspMessage;
extern "C" {
#else
typedef struct rtsp_message rtsp_message;
#endif

void rtsp_dispatch_request(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp);

void msg_free(rtsp_message **msgh);

enum rtsp_read_request_response {
  rtsp_read_request_response_ok,
  rtsp_read_request_response_pending,
  rtsp_read_request_response_immediate_shutdown_requested,
  rtsp_read_request_response_bad_packet,
  rtsp_read_request_response_channel_closed,
  rtsp_read_request_response_read_error,
  rtsp_read_request_response_error
};
enum rtsp_read_request_response rtsp_read_request(rtsp_conn_info *conn, rtsp_message **message);
int msg_write_response(rtsp_conn_info *conn, rtsp_message *message);

void rtsp_listener_stop(void);

// this can be used to [try to] forcibly stop a play session
// play_lock_r get_play_lock(rtsp_conn_info *conn, int allow_session_interruption);
// this will release the play lock only if the conn has it or if the conn is NULL

void stop_play(); // stop and drop a playing connection

void generateTxtDataValueInfo(rtsp_conn_info *conn, void **response, size_t *responseLength);
plist_t generateInfoPlist(rtsp_conn_info *conn);
char *plist_as_xml_text(plist_t the_plist); // caller must free the returned NUL-terminated string

#ifdef __cplusplus
}
#endif

#endif // _RTSP_H

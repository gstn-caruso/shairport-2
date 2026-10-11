#ifndef _AP2_EVENT_RECEIVER_H
#define _AP2_EVENT_RECEIVER_H

#include <expected>
struct SessionState;
// Consumes the bound listener. Stop joins all event work before cipher release.
std::expected<void,int> start_ap2_event_worker(SessionState *session,int listener);
void stop_ap2_event_worker(SessionState *session);

#endif // _AP2_EVENT_RECEIVER_H

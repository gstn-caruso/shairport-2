/*
 * Apple AirPlay 2 Event Receiver. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2014--2026
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
#include "protocol/ap2/ap2_event_receiver.h"
#include "protocol/ap2/ap2_event_message_handler.h"
#include "protocol/ap2/native_event_channel.hpp"
#include "runtime/common.h"


void stop_ap2_event_worker(SessionState *conn) {
  conn->eventPortWorker.stop();

  if (conn->airplay_gid != NULL) {
    free(conn->airplay_gid);
    conn->airplay_gid = NULL;
  }
  conn->groupContainsGroupLeader = 0;
  if (conn->dacp_active_remote != NULL) {
    free(conn->dacp_active_remote);
    conn->dacp_active_remote = NULL;
  }
  debug(2, "Connection %d: AP2 Event Receiver Cleanup is complete.", conn->connection_number);
}

std::expected<void,int> start_ap2_event_worker(SessionState *conn,int listener) {
  return conn->eventPortWorker.start(listener,conn->eventCommandExchange,
      [conn](int accepted) {
        return makeInterruptibleNativeEventChannel(accepted,conn->pairingSession.eventChannel());
      },[conn] {
        debug(2,"Connection %d: AP2 Event Receiver accepted a connection",conn->connection_number);
        ap2_event_send_update_info(conn);
      });
}

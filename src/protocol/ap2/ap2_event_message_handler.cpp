/*
 * AirPlay 2 Event Port Message Handler. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025
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
#include "protocol/ap2/ap2_event_message_handler.h"
#include "runtime/common.h"
#include "protocol/rtsp/rtsp.h"
#include "platform/utilities/generate_random_uuid.h"

void decodeAndLogPlist(plist_t plist_to_log) {
  if (plist_to_log != NULL) {
    char *plist_as_string = plist_as_xml_text(plist_to_log);
    if (plist_as_string != NULL) {
      debug(3, "--\n%s\n--\n", plist_as_string);
      free(plist_as_string);
    }
  }
}

class RuntimeEventObserver final : public EventObserver {
public:
  explicit RuntimeEventObserver(int connection) : connection_(connection) {}
  void observe(const EventDiagnostic &event) override {
    switch (event.kind) {
    case EventDiagnosticKind::notOpen:
      debug(1,"Connection %d: attempt to send a command to the event port over a closed socket",connection_);
      break;
    case EventDiagnosticKind::written:
      debug(3,"Connection %d: Packet of %zu bytes successfully written on the Event Port.",
            connection_,static_cast<size_t>(event.count));
      break;
    case EventDiagnosticKind::read:
      debug(3,"Connection %d: Packet of %zu bytes successfully read on the Event Port.",
            connection_,static_cast<size_t>(event.count));
      break;
    case EventDiagnosticKind::reply:
      debug(3,"Connection %d: Packet Received on Event Port with contents: \n--\n%.*s\n--\n",
            connection_,static_cast<int>(event.bytes.size()),reinterpret_cast<const char *>(event.bytes.data()));
      break;
    case EventDiagnosticKind::closed:
      debug(2,"Connection %d: Event Port connection closed by client",connection_);
      break;
    }
  }
private:
  int connection_;
};

ssize_t ap2_event_port_post_command(rtsp_conn_info *conn,plist_t command) {
  decodeAndLogPlist(command);
  char *encoded = nullptr;
  uint32_t length = 0;
  plist_to_bin(command,&encoded,&length);
  std::unique_ptr<char,decltype(&std::free)> binary(encoded,std::free);
  if (!binary) return 0;
  RuntimeEventObserver observer(conn->connection_number);
  const auto result = conn->eventCommandExchange.postBinaryCommand(
      std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(binary.get()),length),observer);
  debug(3,"Connection %d: POST /command sent on the event port. Result is %zd.",
        conn->connection_number,result.count);
  return result.count;
}

ssize_t ap2_event_send_update_info(rtsp_conn_info *conn) {
  // sends the updateInfo plist on the event port
  ssize_t result = -1;
  plist_t value_plist = generateInfoPlist(conn);
  if (value_plist != NULL) {
    void *txtData = NULL;
    size_t txtDataLength = 0;
    generateTxtDataValueInfo(conn, &txtData, &txtDataLength);
    plist_dict_set_item(value_plist, "txtAirPlay",
                        plist_new_data(static_cast<const char *>(txtData), txtDataLength));
    free(txtData);
    plist_t update_info_plist = plist_new_dict();
    if (update_info_plist != NULL) {
      plist_dict_set_item(update_info_plist, "type", plist_new_string("updateInfo"));
      plist_dict_set_item(update_info_plist, "value", value_plist);

      char *plist_as_string = plist_as_xml_text(update_info_plist);
      if (plist_as_string != NULL) {
        debug(3, "update_info_plist is:\n--\n\"%s\"\n--\n", plist_as_string);
        free(plist_as_string);
      }

      result = ap2_event_port_post_command(conn, update_info_plist);
      plist_free(update_info_plist);
    } else {
      debug(1, "Could not build an updateInfo plist");
    }
  } else {
    debug(1, "Could not build an updateInfo value plist");
  }
  return result;
}

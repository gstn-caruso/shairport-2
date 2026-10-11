#include "session/session_state.hpp"
#include "protocol/rtsp/rtsp.h"
#include "protocol/rtsp/rtsp_message.hpp"
#include "platform/utilities/rtsp_message_utilities.h"
#include "protocol/ap2/ap2_event_receiver.h"
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <unistd.h>

extern "C" int __wrap_ptp_shm_interface_open() { return 0; }
extern "C" void __wrap_build_bonjour_strings(rtsp_conn_info *) {}
extern "C" void __wrap_mdns_update(const char **,const char **) {}

TEST(EventSetup, PtpResponseAdvertisesOwnedListenerAndStopClearsRuntimeGroup) {
  SessionState session{};
  session.thread = pthread_self(); session.connection_ip_family = AF_INET;
  strcpy(session.self_ip_string,"127.0.0.1"); strcpy(session.client_ip_string,"127.0.0.1");
  RtspMessage request,response; request.request("SETUP");
  auto setup = plist_new_dict();
  plist_dict_set_item(setup,"timingProtocol",plist_new_string("PTP"));
  plist_dict_set_item(setup,"groupUUID",plist_new_string("ordinary-group"));
  plist_dict_set_item(setup,"groupContainsGroupLeader",plist_new_bool(1));
  auto timing = plist_new_dict();
  auto addresses = plist_new_array(); plist_array_append_item(addresses,plist_new_string("127.0.0.1"));
  plist_dict_set_item(timing,"Addresses",addresses); plist_dict_set_item(setup,"timingPeerInfo",timing);
  replaceBodyWithPlist(request,setup); plist_free(setup);
  rtsp_dispatch_request(&session,&request,&response);
  ASSERT_EQ(response.responseCode(),200);
  auto result = plistFromMessageBody(response); ASSERT_NE(result,nullptr);
  uint64_t port = 0,timingPort = 1;
  plist_get_uint_val(plist_dict_get_item(result,"eventPort"),&port);
  plist_get_uint_val(plist_dict_get_item(result,"timingPort"),&timingPort);
  EXPECT_NE(plist_dict_get_item(result,"timingPeerInfo"),nullptr); plist_free(result);
  ASSERT_GT(port,0u); EXPECT_EQ(port,session.local_event_port); EXPECT_EQ(timingPort,0u);
  EXPECT_STREQ(session.airplay_gid,"ordinary-group"); EXPECT_EQ(session.groupContainsGroupLeader,1);
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
  const int candidate = socket(AF_INET,SOCK_STREAM,0); ASSERT_GE(candidate,0);
  EXPECT_EQ(bind(candidate,reinterpret_cast<sockaddr *>(&address),sizeof(address)),-1);
  EXPECT_EQ(errno,EADDRINUSE);
  session.dacp_active_remote = strdup("remote");
  stop_ap2_event_worker(&session);
  EXPECT_EQ(bind(candidate,reinterpret_cast<sockaddr *>(&address),sizeof(address)),0); close(candidate);
  EXPECT_EQ(session.airplay_gid,nullptr); EXPECT_EQ(session.dacp_active_remote,nullptr);
  EXPECT_EQ(session.groupContainsGroupLeader,0);
  stop_play(); session.sessionPlist = nullptr;
}

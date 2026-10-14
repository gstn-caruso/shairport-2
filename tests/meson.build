gtest_main = dependency('gtest_main', version: '>=1.17')

gtest = dependency('gtest', version: '>=1.17')

pairing_test_directory = static_library(
    'pairing-test-directory',
    ['pairing_test_directory.cpp'],
    dependencies: receiver_pairing_directory_dep,
)

pairing_test_directory_dep = declare_dependency(
    link_with: pairing_test_directory,
    dependencies: receiver_pairing_directory_dep,
)

buffered_tcp_transport_test = executable(
    'buffered-tcp-transport-test',
    ['buffered_tcp_transport_test.cpp'],
    dependencies: [receiver_buffered_tcp_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=eventfd'],
)

test(
    'buffered-tcp-transport-test',
    buffered_tcp_transport_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

bounded_byte_queue_test = executable(
    'bounded-byte-queue-test',
    ['bounded_byte_queue_test.cpp'],
    dependencies: [receiver_byte_queue_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'bounded-byte-queue-test',
    bounded_byte_queue_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_transport_legacy_test = executable(
    'buffered-transport-legacy-test',
    ['buffered_transport_legacy_test.cpp'],
    dependencies: [receiver_byte_queue_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-transport-legacy-test',
    buffered_transport_legacy_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_block_input_test = executable(
    'buffered-block-input-test',
    ['buffered_block_input_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-block-input-test',
    buffered_block_input_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_processor_lifecycle_test = executable(
    'buffered-processor-lifecycle-test',
    ['buffered_processor_lifecycle_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=have_ptp_timing_information',
        '-Wl,--wrap=reset_buffer',
    ],
)

test(
    'buffered-processor-lifecycle-test',
    buffered_processor_lifecycle_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_playback_adapter_test = executable(
    'buffered-playback-adapter-test',
    ['buffered_playback_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=have_ptp_timing_information',
        '-Wl,--wrap=reset_buffer',
        '-Wl,--wrap=frame_to_local_time',
        '-Wl,--wrap=get_absolute_time_in_ns',
        '-Wl,--wrap=player_put_packet',
        '-Wl,--wrap=usleep',
        '-Wl,--wrap=_Z20prepareIncomingAudioR12SessionState9ssrc_type',
        '-Wl,--wrap=get_ptp_anchor_local_time_info',
        '-Wl,--wrap=crypto_aead_chacha20poly1305_ietf_decrypt',
    ],
)

test(
    'buffered-playback-adapter-test',
    buffered_playback_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_playback_policy_test = executable(
    'buffered-playback-policy-test',
    ['buffered_playback_policy_test.cpp'],
    dependencies: [receiver_buffered_playback_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-playback-policy-test',
    buffered_playback_policy_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_coordinator_test = executable(
    'buffered-coordinator-test',
    ['buffered_coordinator_test.cpp'],
    dependencies: [receiver_buffered_coordinator_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-coordinator-test',
    buffered_coordinator_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

realtime_audio_adapter_test = executable(
    'realtime-audio-adapter-test',
    ['realtime_audio_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=player_put_packet'],
)

test(
    'realtime-audio-adapter-test',
    realtime_audio_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

realtime_audio_test = executable(
    'realtime-audio-test',
    ['realtime_audio_test.cpp'],
    dependencies: [receiver_realtime_audio_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'realtime-audio-test',
    realtime_audio_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

control_packet_adapter_test = executable(
    'control-packet-adapter-test',
    ['control_packet_adapter_test.cpp'],
    dependencies: [receiver_dep, pairing_test_directory_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=_ZN8RtpClock9setAnchorEmjmm',
        '-Wl,--wrap=ptp_get_clock_info',
    ],
)

test(
    'control-packet-adapter-test',
    control_packet_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

resend_adapter_test = executable(
    'resend-adapter-test',
    ['resend_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=recvfrom',
        '-Wl,--wrap=_ZN17RemoteControlPeer15captureIfAbsentEPK8sockaddrj',
        '-Wl,--wrap=get_absolute_time_in_ns',
        '-Wl,--wrap=setsockopt',
        '-Wl,--wrap=sendto',
    ],
)

test(
    'resend-adapter-test',
    resend_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

control_datagram_channel_test = executable(
    'control-datagram-channel-test',
    ['control_datagram_channel_test.cpp'],
    dependencies: [receiver_control_datagram_channel_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=sendto',
        '-Wl,--wrap=setsockopt',
        '-Wl,--wrap=poll',
        '-Wl,--wrap=write',
    ],
)

test(
    'control-datagram-channel-test',
    control_datagram_channel_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

event_command_exchange_test = executable(
    'event-command-exchange-test',
    ['event_command_exchange_test.cpp'],
    dependencies: [receiver_event_command_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'event-command-exchange-test',
    event_command_exchange_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

event_port_worker_test = executable(
    'event-port-worker-test',
    ['event_port_worker_test.cpp'],
    dependencies: [receiver_event_port_worker_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'event-port-worker-test',
    event_port_worker_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

encrypted_channel_test = executable(
    'encrypted-channel-test',
    ['encrypted_channel_test.cpp'],
    include_directories: include_directories('../pair_ap'),
    dependencies: [receiver_encrypted_channel_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'encrypted-channel-test',
    encrypted_channel_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pairing_session_adapter_test = executable(
    'pairing-session-adapter-test',
    ['pairing_session_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=pair_setup_new', '-Wl,--wrap=pair_verify_new'],
)

test(
    'pairing-session-adapter-test',
    pairing_session_adapter_test,
    timeout: 10,
    protocol: 'gtest',
    suite: 'unit',
)

pairing_session_test = executable(
    'pairing-session-test',
    ['pairing_session_test.cpp'],
    dependencies: [receiver_pairing_session_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=pair_cipher_new',
        '-Wl,--wrap=pair_setup_new',
        '-Wl,--wrap=pair_verify_new',
    ],
)

test(
    'pairing-session-test',
    pairing_session_test,
    timeout: 10,
    protocol: 'gtest',
    suite: 'unit',
)

pairing_adapter_test = executable(
    'pairing-adapter-test',
    ['pairing_adapter_test.cpp'],
    dependencies: [receiver_dep, pairing_test_directory_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'pairing-adapter-test',
    pairing_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pairing_directory_test = executable(
    'pairing-directory-test',
    ['pairing_directory_test.cpp'],
    dependencies: [receiver_pairing_directory_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'pairing-directory-test',
    pairing_directory_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pairing_management_test = executable(
    'pairing-management-test',
    ['pairing_management_test.cpp'],
    dependencies: [
        receiver_pairing_management_dep,
        receiver_pairing_directory_dep,
        gtest_main,
    ],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=pair_list'],
)

test(
    'pairing-management-test',
    pairing_management_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

event_setup_test = executable(
    'event-setup-test',
    ['event_setup_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=ptp_shm_interface_open',
        '-Wl,--wrap=build_bonjour_strings',
        '-Wl,--wrap=mdns_update',
    ],
)

test(
    'event-setup-test',
    event_setup_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

event_command_adapter_test = executable(
    'event-command-adapter-test',
    ['event_command_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=ap2_event_send_update_info',
        '-Wl,--wrap=read',
        '-Wl,--wrap=write',
        '-Wl,--wrap=pair_cipher_free',
        '-Wl,--wrap=pair_setup_free',
        '-Wl,--wrap=pair_verify_free',
    ],
)

test(
    'event-command-adapter-test',
    event_command_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

resend_coordinator_test = executable(
    'resend-coordinator-test',
    ['resend_coordinator_test.cpp'],
    dependencies: [receiver_resend_coordinator_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'resend-coordinator-test',
    resend_coordinator_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

remote_control_peer_test = executable(
    'remote-control-peer-test',
    ['remote_control_peer_test.cpp'],
    dependencies: [receiver_remote_control_peer_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'remote-control-peer-test',
    remote_control_peer_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

control_packet_test = executable(
    'control-packet-test',
    ['control_packet_test.cpp'],
    dependencies: [receiver_control_packet_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'control-packet-test',
    control_packet_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

shared_rtp_timing_settings_test = executable(
    'shared-rtp-timing-settings-test',
    ['shared_rtp_timing_settings_test.cpp'],
    dependencies: [receiver_rtp_timing_settings_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'shared-rtp-timing-settings-test',
    shared_rtp_timing_settings_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

session_timing_settings_test = executable(
    'session-timing-settings-test',
    ['session_timing_settings_test.cpp'],
    dependencies: [receiver_dep, pairing_test_directory_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'session-timing-settings-test',
    session_timing_settings_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_block_test = executable(
    'buffered-block-test',
    ['buffered_block_test.cpp'],
    dependencies: [receiver_buffered_block_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-block-test',
    buffered_block_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_block_adapter_test = executable(
    'buffered-block-adapter-test',
    ['buffered_block_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-block-adapter-test',
    buffered_block_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_flush_policy_test = executable(
    'buffered-flush-policy-test',
    ['buffered_flush_policy_test.cpp'],
    dependencies: [receiver_buffered_flush_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-flush-policy-test',
    buffered_flush_policy_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

buffered_flush_adapter_test = executable(
    'buffered-flush-adapter-test',
    ['buffered_flush_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'buffered-flush-adapter-test',
    buffered_flush_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtsp_request_reader_test = executable(
    'rtsp-request-reader-test',
    ['rtsp_request_reader_test.cpp', 'rtsp_request_allocation_test.cpp'],
    dependencies: [receiver_rtsp_request_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=_Znwm', '-Wl,--wrap=_Znam'],
)

test(
    'rtsp-request-reader-test',
    rtsp_request_reader_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtsp_request_adapter_test = executable(
    'rtsp-request-adapter-test',
    ['rtsp_request_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'rtsp-request-adapter-test',
    rtsp_request_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

principal_selection_test = executable(
    'principal-selection-test',
    ['principal_selection_test.cpp'],
    dependencies: [receiver_principal_selection_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'principal-selection-test',
    principal_selection_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

runtime_principal_session_test = executable(
    'runtime-principal-session-test',
    ['runtime_principal_session_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'runtime-principal-session-test',
    runtime_principal_session_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pcm_output_queue_test = executable(
    'pcm-output-queue-test',
    ['pcm_output_queue_test.cpp'],
    dependencies: [receiver_output_queue_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'pcm-output-queue-test',
    pcm_output_queue_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pa_output_test = executable(
    'pa-output-test',
    ['pa_output_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=pa_threaded_mainloop_lock',
        '-Wl,--wrap=pa_threaded_mainloop_unlock',
        '-Wl,--wrap=pa_threaded_mainloop_free',
        '-Wl,--wrap=pa_stream_new',
        '-Wl,--wrap=pa_stream_set_state_callback',
        '-Wl,--wrap=pa_stream_set_write_callback',
        '-Wl,--wrap=pa_stream_connect_playback',
        '-Wl,--wrap=pa_stream_get_state',
        '-Wl,--wrap=pa_stream_is_corked',
        '-Wl,--wrap=pa_stream_is_suspended',
        '-Wl,--wrap=pa_stream_cork',
        '-Wl,--wrap=pa_stream_flush',
        '-Wl,--wrap=pa_stream_disconnect',
        '-Wl,--wrap=pa_stream_unref',
        '-Wl,--wrap=pa_stream_begin_write',
        '-Wl,--wrap=pa_stream_write',
        '-Wl,--wrap=pa_stream_writable_size',
        '-Wl,--wrap=pa_stream_cancel_write',
    ],
)

test(
    'pa-output-test',
    pa_output_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtsp_listener_test = executable(
    'rtsp-listener-test',
    ['rtsp_listener_test.cpp'],
    dependencies: [receiver_dep, pairing_test_directory_dep, gtest],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=poll',
        '-Wl,--wrap=accept4',
        '-Wl,--wrap=build_bonjour_strings',
        '-Wl,--wrap=mdns_register',
        '-Wl,--wrap=bind',
        '-Wl,--wrap=exit_request',
        '-Wl,--wrap=_ZN12SessionState6createESt10shared_ptrI23SharedRtpTimingSettingsES0_I20PairingDirectoryPortES0_IK14PairingOptionsE',
        '-Wl,--wrap=_ZN15SessionRegistry12markFinishedEi',
    ],
)

test(
    'rtsp-listener-test',
    rtsp_listener_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

nqptp_integration_test = executable(
    'nqptp-integration-test',
    ['nqptp_integration_test.cpp'],
    dependencies: [nqptp_core_dep, receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'nqptp-integration-test',
    nqptp_integration_test,
    timeout: 10,
    protocol: 'gtest',
    suite: 'unit',
)

nqptp_runtime_test = executable(
    'nqptp-runtime-test',
    ['nqptp_runtime_test.cpp'],
    dependencies: [nqptp_core_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'nqptp-runtime-test',
    nqptp_runtime_test,
    timeout: 10,
    protocol: 'gtest',
    suite: 'unit',
)

nqptp_timing_test = executable(
    'nqptp-timing-test',
    ['nqptp_timing_test.cpp'],
    dependencies: [nqptp_core_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'nqptp-timing-test',
    nqptp_timing_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

startup_options_test = executable(
    'startup-options-test',
    ['startup_options_test.cpp'],
    dependencies: [receiver_configuration_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'startup-options-test',
    startup_options_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

configuration_loader_test = executable(
    'configuration-loader-test',
    ['configuration_loader_test.cpp'],
    dependencies: [receiver_configuration_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'configuration-loader-test',
    configuration_loader_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

configuration_lease_test = executable(
    'configuration-lease-test',
    ['configuration_lease_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'configuration-lease-test',
    configuration_lease_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pair_tlv_test = executable(
    'pair-tlv-test',
    ['pair_tlv_test.cpp'],
    dependencies: [pair_ap_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'pair-tlv-test',
    pair_tlv_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pair_cipher_bundle_test = executable(
    'pair-cipher-bundle-test',
    ['pair_cipher_bundle_test.cpp'],
    dependencies: [receiver_encrypted_channel_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=pair_cipher_free'],
)

test(
    'pair-cipher-bundle-test',
    pair_cipher_bundle_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

playback_run_test = executable(
    'playback-run-test',
    ['playback_run_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'playback-run-test',
    playback_run_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

playback_timing_test = executable(
    'playback-timing-test',
    ['playback_timing_test.cpp'],
    dependencies: [receiver_playback_timing_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'playback-timing-test',
    playback_timing_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

volume_transaction_test = executable(
    'volume-transaction-test',
    ['volume_transaction_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=pthread_mutex_lock'],
)

test(
    'volume-transaction-test',
    volume_transaction_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

volume_command_cancel_test = executable(
    'volume-command-cancel-test',
    ['volume_command_cancel_test.cpp'],
    dependencies: [receiver_dep, gtest],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=ptp_send_control_message_string'],
)

test(
    'volume-command-cancel-test',
    volume_command_cancel_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

volume_adapter_test = executable(
    'volume-adapter-test',
    ['volume_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'volume-adapter-test',
    volume_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

principal_volume_test = executable(
    'principal-volume-test',
    ['principal_volume_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'principal-volume-test',
    principal_volume_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

volume_control_test = executable(
    'volume-control-test',
    ['volume_control_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'volume-control-test',
    volume_control_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

volume_policy_test = executable(
    'volume-policy-test',
    ['volume_policy_test.cpp'],
    dependencies: [receiver_volume_policy_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'volume-policy-test',
    volume_policy_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

volume_quantities_test = executable(
    'volume-quantities-test',
    ['volume_quantities_test.cpp'],
    dependencies: [receiver_volume_policy_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'volume-quantities-test',
    volume_quantities_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

statistics_formatter_test = executable(
    'statistics-formatter-test',
    ['statistics_formatter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'statistics-formatter-test',
    statistics_formatter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

playback_statistics_test = executable(
    'playback-statistics-test',
    ['playback_statistics_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'playback-statistics-test',
    playback_statistics_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

playback_sync_test = executable(
    'playback-sync-test',
    ['playback_sync_test.cpp'],
    dependencies: [receiver_playback_timing_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'playback-sync-test',
    playback_sync_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

player_volume_wait_test = executable(
    'player-volume-wait-test',
    ['player_volume_wait_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=pthread_cond_timedwait',
        '-Wl,--wrap=_ZN14PlaybackTiming9onArrivalE11ArrivalKind',
        '-Wl,--wrap=get_absolute_time_in_ns',
        '-Wl,--wrap=have_timestamp_timing_information',
        '-Wl,--wrap=frame_to_local_time',
        '-Wl,--wrap=local_time_to_frame',
        '-Wl,--wrap=ptp_send_control_message_string',
    ],
)

test(
    'player-volume-wait-test',
    player_volume_wait_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

playback_samples_test = executable(
    'playback-samples-test',
    ['playback_samples_test.cpp'],
    dependencies: [receiver_pcm_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'playback-samples-test',
    playback_samples_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

pcm_encoder_test = executable(
    'pcm-encoder-test',
    ['pcm_encoder_test.cpp'],
    dependencies: [receiver_pcm_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'pcm-encoder-test',
    pcm_encoder_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

audio_packet_wait_test = executable(
    'audio-packet-wait-test',
    ['audio_packet_wait_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=pthread_cond_timedwait', '-Wl,--wrap=av_frame_free'],
)

test(
    'audio-packet-wait-test',
    audio_packet_wait_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

retransmission_planner_test = executable(
    'retransmission-planner-test',
    ['retransmission_planner_test.cpp'],
    dependencies: [receiver_packets_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'retransmission-planner-test',
    retransmission_planner_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

audio_packet_buffer_test = executable(
    'audio-packet-buffer-test',
    ['audio_packet_buffer_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'audio-packet-buffer-test',
    audio_packet_buffer_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

player_packet_test = executable(
    'player-packet-test',
    ['player_packet_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'player-packet-test',
    player_packet_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

converted_audio_test = executable(
    'converted-audio-test',
    ['converted_audio_test.cpp'],
    dependencies: [receiver_pcm_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'converted-audio-test',
    converted_audio_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

channel_mapping_test = executable(
    'channel-mapping-test',
    ['channel_mapping_test.cpp'],
    dependencies: [receiver_audio_format_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'channel-mapping-test',
    channel_mapping_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

resampler_test = executable(
    'resampler-test',
    ['resampler_test.cpp'],
    dependencies: [receiver_audio_resampler_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=swr_init', '-Wl,--wrap=swr_free'],
)

test(
    'resampler-test',
    resampler_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

audio_decoder_test = executable(
    'audio-decoder-test',
    ['audio_decoder_test.cpp'],
    dependencies: [receiver_audio_decoder_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: [
        '-Wl,--wrap=avcodec_open2',
        '-Wl,--wrap=avcodec_send_packet',
        '-Wl,--wrap=avcodec_free_context',
        '-Wl,--wrap=av_frame_free',
    ],
)

test(
    'audio-decoder-test',
    audio_decoder_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

player_audio_boundary_test = executable(
    'player-audio-boundary-test',
    ['player_audio_boundary_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'player-audio-boundary-test',
    player_audio_boundary_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

output_setup_test = executable(
    'output-setup-test',
    ['output_setup_test.cpp'],
    dependencies: [receiver_output_setup_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=swr_init'],
)

test(
    'output-setup-test',
    output_setup_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

audio_format_test = executable(
    'audio-format-test',
    ['audio_format_test.cpp'],
    dependencies: [receiver_audio_format_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'audio-format-test',
    audio_format_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

audio_input_state_test = executable(
    'audio-input-state-test',
    ['audio_input_state_test.cpp'],
    dependencies: [receiver_audio_format_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'audio-input-state-test',
    audio_input_state_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

session_shutdown_test = executable(
    'session-shutdown-test',
    ['session_shutdown_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'session-shutdown-test',
    session_shutdown_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

session_replacement_test = executable(
    'session-replacement-test',
    ['session_replacement_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'session-replacement-test',
    session_replacement_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

session_registry_test = executable(
    'session-registry-test',
    ['session_registry_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'session-registry-test',
    session_registry_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

managed_session_registry_test = executable(
    'managed-session-registry-test',
    ['managed_session_registry_test.cpp'],
    dependencies: [receiver_session_registry_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'managed-session-registry-test',
    managed_session_registry_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtp_clock_test = executable(
    'rtp-clock-test',
    ['rtp_clock_test.cpp'],
    dependencies: [receiver_rtp_clock_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'rtp-clock-test',
    rtp_clock_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtsp_message_c_test = executable(
    'rtsp-message-c-test',
    ['rtsp_message_c_test.c'],
    dependencies: [receiver_dep],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_language: 'cpp',
)

rtsp_message_test = executable(
    'rtsp-message-test',
    ['rtsp_message_test.cpp'],
    dependencies: [receiver_rtsp_message_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'rtsp-message-test',
    rtsp_message_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtsp_parameters_test = executable(
    'rtsp-parameters-test',
    ['rtsp_parameters_test.cpp'],
    dependencies: [receiver_rtsp_parameters_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'rtsp-parameters-test',
    rtsp_parameters_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

rtsp_message_adapter_test = executable(
    'rtsp-message-adapter-test',
    ['rtsp_message_adapter_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_args: ['-Wl,--wrap=plist_free'],
)

test(
    'rtsp-message-adapter-test',
    rtsp_message_adapter_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

receiver_encoding_cpp_test = executable(
    'receiver-encoding-cpp-test',
    ['receiver_encoding_cpp_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'receiver-encoding-cpp-test',
    receiver_encoding_cpp_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

activity_state_test = executable(
    'activity-state-test',
    ['activity_state_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'activity-state-test',
    activity_state_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

activity_monitor_test = executable(
    'activity-monitor-test',
    ['activity_monitor_test.c'],
    dependencies: [receiver_dep],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_language: 'cpp',
)

activity_monitor_lifecycle_test = executable(
    'activity-monitor-lifecycle-test',
    ['activity_monitor_lifecycle_test.cpp'],
    dependencies: [receiver_dep, gtest],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'activity-monitor-lifecycle-test',
    activity_monitor_lifecycle_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

string_utilities_cpp_test = executable(
    'string-utilities-cpp-test',
    ['string_utilities_cpp_test.cpp'],
    dependencies: [receiver_text_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'string-utilities-cpp-test',
    string_utilities_cpp_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

string_utilities_test = executable(
    'string-utilities-test',
    ['string_utilities_test.c'],
    dependencies: [receiver_dep],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_language: 'cpp',
)

structured_buffer_cpp_test = executable(
    'structured-buffer-cpp-test',
    ['structured_buffer_cpp_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'structured-buffer-cpp-test',
    structured_buffer_cpp_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

structured_buffer_test = executable(
    'structured-buffer-test',
    ['structured_buffer_test.c'],
    dependencies: [receiver_dep],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
    link_language: 'cpp',
)

receiver_linkage_test = executable(
    'receiver-linkage-test',
    ['receiver_linkage_test.cpp'],
    dependencies: [receiver_dep],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

rtsp_dispatch_test = executable(
    'rtsp-dispatch-test',
    ['rtsp_dispatch_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'rtsp-dispatch-test',
    rtsp_dispatch_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

foreach entry : [
    ['rtsp-message-c', rtsp_message_c_test],
    ['activity-monitor', activity_monitor_test],
    ['string-utilities', string_utilities_test],
    ['structured-buffer', structured_buffer_test],
]
    test(entry[0], entry[1], timeout: 5, suite: 'contract')
endforeach
test(
    'companion-version',
    find_program('sh'),
    args: [
        files('companion_cli_test.sh'),
        companion_binary,
        meson.project_version(),
    ],
    timeout: 5,
    suite: 'contract',
)
test(
    'companion-help',
    companion_binary,
    args: '--help',
    timeout: 5,
    suite: 'contract',
)
foreach name : ['nqptp', 'daemon-startup', 'removed-options']
    test(
        name,
        find_program('sh'),
        args: [files(name.replace('-', '_') + '_test.sh'), receiver_binary],
        timeout: 30,
        suite: 'contract',
    )
endforeach
test(
    'daemon-interface',
    find_program('sh'),
    args: [
        files('daemon_interface_test.sh'),
        receiver_binary,
        sysconfdir / 'shairport-2.conf',
    ],
    timeout: 30,
    suite: 'contract',
)
test(
    'configure-contract',
    find_program('sh'),
    args: [
        files('meson_configure_test.sh'),
        meson.project_build_root(),
        c.cmd_array()[0],
        cpp.cmd_array()[0],
        receiver_linkage_test,
    ],
    timeout: 120,
    suite: 'contract',
)

rtsp_volume_test = executable(
    'rtsp-volume-test',
    ['rtsp_volume_test.cpp'],
    dependencies: [receiver_dep, gtest_main],
    cpp_args: ['-UNDEBUG'],
    c_args: ['-UNDEBUG'],
)

test(
    'rtsp-volume-test',
    rtsp_volume_test,
    timeout: 5,
    protocol: 'gtest',
    suite: 'unit',
)

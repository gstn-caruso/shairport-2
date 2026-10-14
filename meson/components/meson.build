receiver_audio_format_dep = declare_dependency(
    link_with: receiver_audio_format,
    include_directories: includes,
    dependencies: [],
)
receiver_output_queue_dep = declare_dependency(
    link_with: receiver_output_queue,
    include_directories: includes,
    dependencies: [],
    sources: receiver_output_queue_bmi,
)
receiver_pcm_dep = declare_dependency(
    link_with: receiver_pcm,
    include_directories: includes,
    dependencies: [avutil],
)
receiver_packets_dep = declare_dependency(
    link_with: receiver_packets,
    include_directories: includes,
    dependencies: [],
)
receiver_text_dep = declare_dependency(
    link_with: receiver_text,
    include_directories: includes,
    dependencies: [],
)
receiver_mod23_dep = declare_dependency(
    link_with: receiver_mod23,
    include_directories: includes,
    dependencies: [],
)
receiver_playback_timing_dep = declare_dependency(
    link_with: receiver_playback_timing,
    include_directories: includes,
    dependencies: [threads],
)
receiver_buffered_playback_dep = declare_dependency(
    link_with: receiver_buffered_playback,
    include_directories: includes,
    dependencies: [],
    sources: receiver_buffered_playback_bmi,
)
receiver_buffered_block_dep = declare_dependency(
    link_with: receiver_buffered_block,
    include_directories: includes,
    dependencies: [sodium],
    sources: receiver_buffered_block_bmi,
)
receiver_event_command_dep = declare_dependency(
    link_with: receiver_event_command,
    include_directories: includes,
    dependencies: [threads],
)
receiver_pairing_directory_dep = declare_dependency(
    link_with: receiver_pairing_directory,
    include_directories: includes,
    dependencies: [threads],
    sources: receiver_pairing_directory_bmi,
)
receiver_pairing_management_dep = declare_dependency(
    link_with: receiver_pairing_management,
    include_directories: includes,
    dependencies: [pair_ap_dep],
)
receiver_realtime_audio_dep = declare_dependency(
    link_with: receiver_realtime_audio,
    include_directories: includes,
    dependencies: [sodium],
    sources: receiver_realtime_audio_bmi,
)
receiver_control_packet_dep = declare_dependency(
    link_with: receiver_control_packet,
    include_directories: includes,
    dependencies: [],
    sources: receiver_control_packet_bmi,
)
receiver_resend_coordinator_dep = declare_dependency(
    link_with: receiver_resend_coordinator,
    include_directories: includes,
    dependencies: [threads],
)
receiver_rtsp_message_dep = declare_dependency(
    link_with: receiver_rtsp_message,
    include_directories: includes,
    dependencies: [],
)
receiver_session_registry_dep = declare_dependency(
    link_with: receiver_session_registry,
    include_directories: includes,
    dependencies: [threads],
)
receiver_principal_selection_dep = declare_dependency(
    link_with: receiver_principal_selection,
    include_directories: includes,
    dependencies: [threads],
    sources: receiver_principal_selection_bmi,
)
receiver_rtp_clock_dep = declare_dependency(
    link_with: receiver_rtp_clock,
    include_directories: includes,
    dependencies: [],
)
receiver_rtp_timing_settings_dep = declare_dependency(
    link_with: receiver_rtp_timing_settings,
    include_directories: includes,
    dependencies: [threads],
)
receiver_byte_queue_dep = declare_dependency(
    link_with: receiver_byte_queue,
    include_directories: includes,
    dependencies: [threads],
)
receiver_remote_control_peer_dep = declare_dependency(
    link_with: receiver_remote_control_peer,
    include_directories: includes,
    dependencies: [threads],
)
receiver_encrypted_channel_dep = declare_dependency(
    link_with: receiver_encrypted_channel,
    include_directories: includes,
    dependencies: [pair_ap_dep, threads],
)
receiver_volume_policy_dep = declare_dependency(
    link_with: receiver_volume_policy,
    include_directories: includes,
    dependencies: [],
)
receiver_configuration_dep = declare_dependency(
    link_with: receiver_configuration,
    include_directories: includes,
    dependencies: [
        libconfig,
        avutil,
        receiver_text_dep,
        receiver_audio_format_dep,
    ],
)
receiver_audio_decoder_dep = declare_dependency(
    link_with: receiver_audio_decoder,
    include_directories: includes,
    dependencies: [receiver_audio_format_dep, avcodec, avutil, threads],
)
receiver_audio_resampler_dep = declare_dependency(
    link_with: receiver_audio_resampler,
    include_directories: includes,
    dependencies: [receiver_audio_format_dep, swresample, avutil, threads],
)
receiver_buffered_flush_dep = declare_dependency(
    link_with: receiver_buffered_flush,
    include_directories: includes,
    dependencies: [receiver_mod23_dep, threads],
)
receiver_buffered_coordinator_dep = declare_dependency(
    link_with: receiver_buffered_coordinator,
    include_directories: includes,
    dependencies: [
        receiver_buffered_block_dep,
        receiver_buffered_playback_dep,
        receiver_audio_format_dep,
    ],
    sources: receiver_buffered_coordinator_bmi,
)
receiver_rtsp_parameters_dep = declare_dependency(
    link_with: receiver_rtsp_parameters,
    include_directories: includes,
    dependencies: [receiver_rtsp_message_dep],
    sources: receiver_rtsp_parameters_bmi,
)
receiver_rtsp_request_dep = declare_dependency(
    link_with: receiver_rtsp_request,
    include_directories: includes,
    dependencies: [receiver_rtsp_message_dep],
    sources: receiver_rtsp_request_bmi,
)
receiver_pairing_session_dep = declare_dependency(
    link_with: receiver_pairing_session,
    include_directories: includes,
    dependencies: [receiver_encrypted_channel_dep, pair_ap_dep],
)
receiver_buffered_tcp_dep = declare_dependency(
    link_with: receiver_buffered_tcp,
    include_directories: includes,
    dependencies: [receiver_byte_queue_dep, threads],
)
receiver_control_datagram_channel_dep = declare_dependency(
    link_with: receiver_control_datagram_channel,
    include_directories: includes,
    dependencies: [receiver_remote_control_peer_dep, threads],
)
receiver_event_port_worker_dep = declare_dependency(
    link_with: receiver_event_port_worker,
    include_directories: includes,
    dependencies: [receiver_event_command_dep, threads],
)
receiver_output_setup_dep = declare_dependency(
    link_with: receiver_output_setup,
    include_directories: includes,
    dependencies: [receiver_audio_resampler_dep, receiver_pcm_dep],
)
component_deps = [
    receiver_configuration_dep,
    receiver_audio_decoder_dep,
    receiver_audio_format_dep,
    receiver_output_queue_dep,
    receiver_output_setup_dep,
    receiver_pcm_dep,
    receiver_audio_resampler_dep,
    receiver_packets_dep,
    receiver_text_dep,
    receiver_mod23_dep,
    receiver_playback_timing_dep,
    receiver_buffered_flush_dep,
    receiver_buffered_playback_dep,
    receiver_buffered_block_dep,
    receiver_buffered_coordinator_dep,
    receiver_event_command_dep,
    receiver_pairing_directory_dep,
    receiver_pairing_management_dep,
    receiver_realtime_audio_dep,
    receiver_control_packet_dep,
    receiver_resend_coordinator_dep,
    receiver_rtsp_message_dep,
    receiver_rtsp_parameters_dep,
    receiver_rtsp_request_dep,
    receiver_session_registry_dep,
    receiver_principal_selection_dep,
    receiver_pairing_session_dep,
    receiver_rtp_clock_dep,
    receiver_rtp_timing_settings_dep,
    receiver_byte_queue_dep,
    receiver_buffered_tcp_dep,
    receiver_remote_control_peer_dep,
    receiver_control_datagram_channel_dep,
    receiver_event_port_worker_dep,
    receiver_encrypted_channel_dep,
    receiver_volume_policy_dep,
]

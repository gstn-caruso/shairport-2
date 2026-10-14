receiver_sources += files('rtp.cpp')

receiver_realtime_audio = static_library(
    'receiver-realtime-audio',
    files('realtime_audio.cpp') + [receiver_realtime_audio_bmi] + [
        receiver_realtime_audio_object,
    ],
    include_directories: includes,
    dependencies: [sodium],
)

receiver_control_packet = static_library(
    'receiver-control-packet',
    files('control_packet.cpp') + [receiver_control_packet_bmi] + [
        receiver_control_packet_object,
    ],
    include_directories: includes,
)

receiver_resend_coordinator = static_library(
    'receiver-resend-coordinator',
    files('resend_coordinator.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

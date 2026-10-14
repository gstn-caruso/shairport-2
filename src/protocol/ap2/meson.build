receiver_sources += files(
    'ap2_buffered_audio_processor.cpp',
    'ap2_event_message_handler.cpp',
    'ap2_event_receiver.cpp',
    'native_encrypted_channel.cpp',
    'native_event_channel.cpp',
)

receiver_buffered_flush = static_library(
    'receiver-buffered-flush',
    files('buffered_flush_policy.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_buffered_playback = static_library(
    'receiver-buffered-playback',
    files('buffered_playback_policy.cpp') + [receiver_buffered_playback_bmi] + [
        receiver_buffered_playback_object,
    ],
    include_directories: includes,
)

receiver_buffered_block = static_library(
    'receiver-buffered-block',
    files('buffered_audio_block.cpp') + [receiver_buffered_block_bmi] + [
        receiver_buffered_block_object,
    ],
    include_directories: includes,
    dependencies: [sodium],
)

receiver_buffered_coordinator = static_library(
    'receiver-buffered-coordinator',
    files('buffered_block_input.cpp', 'buffered_receiver_coordinator.cpp') + [
        receiver_buffered_coordinator_bmi,
        receiver_buffered_playback_bmi,
        receiver_buffered_block_bmi,
    ] + [
        receiver_buffered_coordinator_object,
    ],
    include_directories: includes,
)

receiver_event_command = static_library(
    'receiver-event-command',
    files('event_command_exchange.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_pairing_directory = static_library(
    'receiver-pairing-directory',
    files('pairing_directory.cpp') + [receiver_pairing_directory_bmi] + [
        receiver_pairing_directory_object,
    ],
    include_directories: includes,
    dependencies: [threads],
)

receiver_pairing_management = static_library(
    'receiver-pairing-management',
    files('pairing_management.cpp'),
    include_directories: includes,
)

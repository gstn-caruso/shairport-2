receiver_sources += files(
    'debug.cpp',
    'exit.cpp',
    'generate_device_uuid.cpp',
    'generate_random_uuid.cpp',
    'network_utilities.cpp',
    'rtsp_message_utilities.cpp',
    'string_utilities.cpp',
    'structured_buffer.cpp',
)

receiver_text = static_library(
    'receiver-text',
    files('service_name_formatter.cpp'),
    include_directories: includes,
)

receiver_mod23 = static_library(
    'receiver-mod23',
    files('mod23.cpp'),
    include_directories: includes,
)

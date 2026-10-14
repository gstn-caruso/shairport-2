receiver_sources += files('ptp-utilities.cpp')

receiver_rtp_clock = static_library(
    'receiver-rtp-clock',
    files('rtp_clock.cpp'),
    include_directories: includes,
    cpp_args: legacy_args,
)

receiver_rtp_timing_settings = static_library(
    'receiver-rtp-timing-settings',
    files('shared_rtp_timing_settings.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

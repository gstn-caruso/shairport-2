receiver_pcm = static_library(
    'receiver-pcm',
    files('pcm_encoder.cpp', 'playback_samples.cpp'),
    include_directories: includes,
    dependencies: [avutil],
    cpp_args: legacy_args,
)

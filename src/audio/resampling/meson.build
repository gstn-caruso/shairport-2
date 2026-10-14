receiver_audio_resampler = static_library(
    'receiver-audio-resampler',
    files('resampler.cpp'),
    include_directories: includes,
    dependencies: [swresample, avutil, threads],
)

receiver_sources += files('audio.cpp', 'audio_pa.cpp')

receiver_output_queue = static_library(
    'receiver-output-queue',
    files('pcm_output_queue.cpp') + [receiver_output_queue_bmi] + [
        receiver_output_queue_object,
    ],
    include_directories: includes,
)

receiver_output_setup = static_library(
    'receiver-output-setup',
    files('output_setup.cpp'),
    include_directories: includes,
)

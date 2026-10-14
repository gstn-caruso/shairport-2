receiver_output_queue_bmi = custom_target(
    'receiver-output-queue-bmi',
    input: '../../src/audio/output/pcm_output_queue.cppm',
    output: 'receiver.audio.output.queue.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.audio.output.queue.pcm.d',
)

receiver_output_queue_object = custom_target(
    'receiver-output-queue-module-object',
    input: receiver_output_queue_bmi,
    output: 'receiver.audio.output.queue.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_buffered_playback_bmi = custom_target(
    'receiver-buffered-playback-bmi',
    input: '../../src/protocol/ap2/buffered_playback_policy.cppm',
    output: 'receiver.protocol.ap2.buffered_playback.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.ap2.buffered_playback.pcm.d',
)

receiver_buffered_playback_object = custom_target(
    'receiver-buffered-playback-module-object',
    input: receiver_buffered_playback_bmi,
    output: 'receiver.protocol.ap2.buffered_playback.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_buffered_block_bmi = custom_target(
    'receiver-buffered-block-bmi',
    input: '../../src/protocol/ap2/buffered_audio_block.cppm',
    output: 'receiver.protocol.ap2.buffered_block.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.ap2.buffered_block.pcm.d',
)

receiver_buffered_block_object = custom_target(
    'receiver-buffered-block-module-object',
    input: receiver_buffered_block_bmi,
    output: 'receiver.protocol.ap2.buffered_block.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_buffered_coordinator_bmi = custom_target(
    'receiver-buffered-coordinator-bmi',
    input: '../../src/protocol/ap2/buffered_receiver_coordinator.cppm',
    output: 'receiver.protocol.ap2.buffered_coordinator.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.ap2.buffered_coordinator.pcm.d',
)

receiver_buffered_coordinator_object = custom_target(
    'receiver-buffered-coordinator-module-object',
    input: receiver_buffered_coordinator_bmi,
    output: 'receiver.protocol.ap2.buffered_coordinator.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_pairing_directory_bmi = custom_target(
    'receiver-pairing-directory-bmi',
    input: '../../src/protocol/ap2/pairing_directory.cppm',
    output: 'receiver.protocol.ap2.pairing_directory.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.ap2.pairing_directory.pcm.d',
)

receiver_pairing_directory_object = custom_target(
    'receiver-pairing-directory-module-object',
    input: receiver_pairing_directory_bmi,
    output: 'receiver.protocol.ap2.pairing_directory.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_realtime_audio_bmi = custom_target(
    'receiver-realtime-audio-bmi',
    input: '../../src/protocol/rtp/realtime_audio.cppm',
    output: 'receiver.protocol.ap2.realtime_audio.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.ap2.realtime_audio.pcm.d',
)

receiver_realtime_audio_object = custom_target(
    'receiver-realtime-audio-module-object',
    input: receiver_realtime_audio_bmi,
    output: 'receiver.protocol.ap2.realtime_audio.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_control_packet_bmi = custom_target(
    'receiver-control-packet-bmi',
    input: '../../src/protocol/rtp/control_packet.cppm',
    output: 'receiver.protocol.ap2.control_packet.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.ap2.control_packet.pcm.d',
)

receiver_control_packet_object = custom_target(
    'receiver-control-packet-module-object',
    input: receiver_control_packet_bmi,
    output: 'receiver.protocol.ap2.control_packet.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_rtsp_parameters_bmi = custom_target(
    'receiver-rtsp-parameters-bmi',
    input: '../../src/protocol/rtsp/rtsp_parameters.cppm',
    output: 'receiver.protocol.rtsp.parameters.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.rtsp.parameters.pcm.d',
)

receiver_rtsp_parameters_object = custom_target(
    'receiver-rtsp-parameters-module-object',
    input: receiver_rtsp_parameters_bmi,
    output: 'receiver.protocol.rtsp.parameters.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_rtsp_request_bmi = custom_target(
    'receiver-rtsp-request-bmi',
    input: '../../src/protocol/rtsp/rtsp_request_reader.cppm',
    output: 'receiver.protocol.rtsp.request.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.protocol.rtsp.request.pcm.d',
)

receiver_rtsp_request_object = custom_target(
    'receiver-rtsp-request-module-object',
    input: receiver_rtsp_request_bmi,
    output: 'receiver.protocol.rtsp.request.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

receiver_principal_selection_bmi = custom_target(
    'receiver-principal-selection-bmi',
    input: '../../src/session/principal_selection.cppm',
    output: 'receiver.session.principal.pcm',
    command: [
        cpp.cmd_array(),
        module_args,
        '--precompile',
        '@INPUT@',
        '-o',
        '@OUTPUT@',
        '-MD',
        '-MF',
        '@DEPFILE@',
    ],
    depfile: 'receiver.session.principal.pcm.d',
)

receiver_principal_selection_object = custom_target(
    'receiver-principal-selection-module-object',
    input: receiver_principal_selection_bmi,
    output: 'receiver.session.principal.o',
    command: [cpp.cmd_array(), module_args, '-c', '@INPUT@', '-o', '@OUTPUT@'],
)

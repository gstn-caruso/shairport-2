receiver_sources += files('rtsp.cpp', 'rtsp_listener.cpp')

receiver_rtsp_message = static_library(
    'receiver-rtsp-message',
    files('rtsp_message.cpp'),
    include_directories: includes,
)

receiver_rtsp_parameters = static_library(
    'receiver-rtsp-parameters',
    files('rtsp_parameters.cpp') + [receiver_rtsp_parameters_bmi] + [
        receiver_rtsp_parameters_object,
    ],
    include_directories: includes,
)

receiver_rtsp_request = static_library(
    'receiver-rtsp-request',
    files('rtsp_request_reader.cpp') + [receiver_rtsp_request_bmi] + [
        receiver_rtsp_request_object,
    ],
    include_directories: includes,
)

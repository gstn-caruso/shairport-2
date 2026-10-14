receiver_byte_queue = static_library(
    'receiver-byte-queue',
    files('bounded_byte_queue.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_buffered_tcp = static_library(
    'receiver-buffered-tcp',
    files('buffered_tcp_transport.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_remote_control_peer = static_library(
    'receiver-remote-control-peer',
    files('remote_control_peer.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_control_datagram_channel = static_library(
    'receiver-control-datagram-channel',
    files('control_datagram_channel.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_event_port_worker = static_library(
    'receiver-event-port-worker',
    files('event_port_worker.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_encrypted_channel = static_library(
    'receiver-encrypted-channel',
    files('encrypted_channel.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

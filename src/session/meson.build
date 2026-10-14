receiver_sources += files('runtime_session_worker.cpp', 'session_state.cpp')

receiver_session_registry = static_library(
    'receiver-session-registry',
    files('session_registry.cpp'),
    include_directories: includes,
    dependencies: [threads],
)

receiver_principal_selection = static_library(
    'receiver-principal-selection',
    files('principal_selection.cpp') + [receiver_principal_selection_bmi] + [
        receiver_principal_selection_object,
    ],
    include_directories: includes,
    dependencies: [threads],
)

receiver_pairing_session = static_library(
    'receiver-pairing-session',
    files('pairing_session.cpp'),
    include_directories: includes,
)

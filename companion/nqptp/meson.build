nqptp_core = static_library(
    'nqptp-core',
    'runtime.cpp',
    'nqptp.cpp',
    'nqptp-clock-sources.cpp',
    'nqptp-message-handlers.cpp',
    'nqptp-utilities.cpp',
    'general-utilities.cpp',
    'debug.cpp',
    include_directories: [includes, include_directories('.')],
    dependencies: threads,
    cpp_args: [
        '-DCONFIG_FOR_LINUX',
        '-DVERSION="' + meson.project_version() + '"',
        '-pedantic-errors',
    ],
)
nqptp_core_dep = declare_dependency(
    link_with: nqptp_core,
    include_directories: [includes, include_directories('.')],
    dependencies: [threads, cpp.find_library('rt')],
)
companion_binary = executable(
    'shairport-2-nqptp',
    'main.cpp',
    dependencies: nqptp_core_dep,
    cpp_args: [
        '-DVERSION="' + meson.project_version() + '"',
        '-pedantic-errors',
    ],
    install: true,
)

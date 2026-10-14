receiver_sources += files('legacy_config_lease.cpp', 'shairport.cpp')

receiver_configuration = static_library(
    'receiver-configuration',
    files(
        'configuration_loader.cpp',
        'configuration_validation.cpp',
        'startup_options.cpp',
    ),
    include_directories: includes,
    dependencies: [libconfig, avutil],
)

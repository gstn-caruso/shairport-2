project(
    'shairport-2',
    'c',
    'cpp',
    version: files('VERSION'),
    meson_version: '>=1.10.0',
    default_options: [
        'cpp_std=c++26',
        'c_std=c17',
        'buildtype=release',
        'warning_level=2',
        'b_ndebug=false',
    ],
)

fs = import('fs')
if host_machine.system() != 'linux'
    error('Linux is the only supported platform')
endif
if fs.exists('config.h')
    error(
        'Use an out-of-tree build and remove any config.h from the source directory',
    )
endif
c = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
if c.get_id() != 'clang' or cpp.get_id() != 'clang' or c.version() != '23.1.3' or cpp.version() != '23.1.3'
    error('Clang 23.1.3 is required; install the pinned asdf toolchain')
endif
if get_option('cpp_std') != 'c++26'
    error('C++26 without extensions is required')
endif
threads = dependency('threads')
probe = cpp.run(
    files('meson/cpp26_probe.cpp'),
    dependencies: threads,
    name: 'C++26 libstdc++ 15 runtime',
)
if not probe.compiled() or probe.returncode() != 0
    error(
        'C++26 expected/span/format/jthread with libstdc++ 15 must compile, link and run',
    )
endif

avutil = dependency('libavutil')
libconfig = dependency('libconfig')
avcodec = dependency('libavcodec')
swresample = dependency('libswresample')
sodium = dependency('libsodium')
gcrypt = dependency('libgcrypt')
plist = dependency('libplist-2.0', version: '>=2.0.0')
receiver_external_deps = [
    threads,
    avutil,
    libconfig,
    avcodec,
    swresample,
    sodium,
    gcrypt,
    plist,
    dependency('popt'),
    dependency('libpulse', version: '>=0.9.2'),
    dependency('avahi-client'),
    dependency('libssl'),
    dependency('libcrypto'),
    dependency('uuid'),
    dependency('libavformat'),
    cpp.find_library('m'),
    cpp.find_library('rt'),
]
find_program('plistutil')
find_program('xxd')
includes = include_directories('.', 'src', 'src/timing')
add_project_arguments(
    '-Wshadow',
    '-Wformat=2',
    '-fno-common',
    '-pthread',
    '-fmodule-file-deps',
    language: 'cpp',
)
bindir = get_option('prefix') / get_option('bindir')
sysconfdir = get_option('prefix') / get_option('sysconfdir')
legacy_args = [
    '-include',
    meson.project_source_root() / 'src/platform/utilities/debug.h',
    '-Wno-multichar',
    '-UNDEBUG',
    '-DHAVE_CONFIG_H',
    '-DSYSCONFDIR="' + sysconfdir + '"',
]
config = configuration_data()
config.set('CONFIG_USE_GIT_VERSION_STRING', get_option('use_git_version'))
config.set('HAVE_LIBPLIST_GE_2_3_0', plist.version().version_compare('>=2.3.0'))
config.set('PROJECT_VERSION', meson.project_version())
configure_file(
    input: 'meson/config.h.in',
    output: 'config.h',
    configuration: config,
)
git_version = vcs_tag(
    input: 'meson/git-version.h.in',
    output: 'cmake-gitversion.h',
    command: ['git', 'describe', '--tags', '--dirty', '--broken', '--always'],
    fallback: 'NA',
)

# Meson's module scanner does not support dotted Clang module names. Explicit
# PCM targets preserve those public names and expose header dependencies to Ninja.
module_args = [
    '-std=c++26',
    '-fPIC',
    '-pthread',
    '-D_GLIBCXX_ASSERTIONS=1',
    '-D_FILE_OFFSET_BITS=64',
    '-O' + get_option('optimization'),
    '-I' + meson.project_source_root(),
    '-I' + meson.project_source_root() / 'src',
    '-I' + meson.project_build_root(),
] + get_option(
    'cpp_args',
)
if get_option('debug')
    module_args += '-g'
endif
sanitizers = get_option('b_sanitize')
if sanitizers != 'none'
    module_args += ['-fsanitize=' + sanitizers, '-fno-omit-frame-pointer']
endif
module_args += run_command(
    find_program('pkg-config'),
    '--cflags',
    'libsodium',
    'libavutil',
    check: true,
).stdout().strip().split()
module_flags = []
foreach name : [
    'receiver.audio.output.queue',
    'receiver.protocol.ap2.buffered_playback',
    'receiver.protocol.ap2.buffered_block',
    'receiver.protocol.ap2.buffered_coordinator',
    'receiver.protocol.ap2.pairing_directory',
    'receiver.protocol.ap2.realtime_audio',
    'receiver.protocol.ap2.control_packet',
    'receiver.protocol.rtsp.parameters',
    'receiver.protocol.rtsp.request',
    'receiver.session.principal',
]
    module_flags += '-fmodule-file=' + name + '=meson/modules/' + name + '.pcm'
endforeach
module_args += module_flags
add_project_arguments(module_flags, language: 'cpp')
subdir('meson/modules')
subdir('plists')
pair_ap = static_library(
    'pair_ap',
    files(
        'pair_ap/pair-tlv.cpp',
        'pair_ap/pair.cpp',
        'pair_ap/pair_fruit.cpp',
        'pair_ap/pair_homekit.cpp',
    ),
    include_directories: includes,
    dependencies: [sodium, gcrypt, plist],
    cpp_args: [
        '-DCONFIG_GCRYPT',
        '-include',
        meson.project_source_root() / 'src/platform/utilities/debug.h',
        '-UNDEBUG',
        '-Wno-multichar',
    ],
)
pair_ap_dep = declare_dependency(
    link_with: pair_ap,
    include_directories: includes,
    dependencies: [sodium, gcrypt, plist],
)
receiver_sources = []
subdir('src')
subdir('meson/components')
receiver = static_library(
    'receiver',
    receiver_sources + plist_sources + [git_version],
    include_directories: includes,
    dependencies: receiver_external_deps + component_deps + [pair_ap_dep],
    cpp_args: legacy_args,
)
receiver_dep = declare_dependency(
    link_with: receiver,
    include_directories: includes,
    dependencies: receiver_external_deps + component_deps + [pair_ap_dep],
)
receiver_binary = executable(
    'shairport-2',
    'src/app/main.cpp',
    dependencies: receiver_dep,
    install: true,
)
subdir('companion/nqptp')
if get_option('tests')
    subdir('tests')
endif
subdir('meson/install')

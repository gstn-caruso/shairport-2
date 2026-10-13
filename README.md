# shairport-2

[![CI](https://github.com/gstn-caruso/shairport-2/actions/workflows/receiver.yml/badge.svg)](https://github.com/gstn-caruso/shairport-2/actions/workflows/receiver.yml)
[![Release](https://img.shields.io/github/v/release/gstn-caruso/shairport-2)](https://github.com/gstn-caruso/shairport-2/releases)
[![License](https://img.shields.io/badge/license-MIT%20%2B%20GPLv2-blue)](LICENSES)
[![Platform](https://img.shields.io/badge/platform-Linux-blue)](#build-and-install)

An open-source AirPlay 2 audio receiver for Linux. Select this receiver on an
AirPlay device to play through the Linux machine's speakers.

This fork supports realtime and buffered playback, encrypted pairing,
sender-controlled volume, and ALAC/AAC stereo and surround streams. It uses
PulseAudio or PipeWire's `pipewire-pulse` server, Avahi for discovery, FFmpeg for
decoding, and a bundled PTP timing companion. AirPlay 1 and other audio backends
are unsupported. The receiver runs as a systemd **user service** in the audio
user's session; the timing companion and Avahi run as system services.

## Build and install

Requirements: Linux, CMake 4.2+, Ninja, pinned Clang 23.1.3, matching
`clang-scan-deps`, libstdc++ 15 and GoogleTest 1.17+ for tests. Production sources
use C++26; the embedded pairing dependency retains a C linkage boundary.
Install the asdf compiler and dependencies from the repository root:

```sh
sudo apt-get install cmake ninja-build g++-15 g++ python3
asdf plugin add clang https://github.com/higebu/asdf-llvm.git
git -C "${ASDF_DATA_DIR:-$HOME/.asdf}/plugins/clang" checkout b7b8dd389c790237145e7436f1cd85b49611e37e
asdf install
asdf reshim clang
sudo apt-get install pkg-config libgtest-dev libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --no-tests=error --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

The asdf plugin builds LLVM from source; CI uses a checksum-verified official
archive. Use a fresh build directory after moving a checkout: caches retain
absolute paths. Remove stale source-tree `config.h` before configuring.
`-DBUILD_TESTING=OFF` skips GoogleTest. Choose installation paths with
`CMAKE_INSTALL_PREFIX` and `CMAKE_INSTALL_SYSCONFDIR`;
`-DINSTALL_CONFIG_FILES=OFF` skips the sample. Legacy provider and
`WITH_`, `WITHOUT_`, `CONFIG_` build switches are rejected.

Inspect the staged files, then install and preserve any existing configuration:

```sh
sudo cmake --install build/cmake
sudo cp --no-clobber /etc/shairport-2.conf.sample /etc/shairport-2.conf
```

### Debian packages

CI artifacts and [releases](https://github.com/gstn-caruso/shairport-2/releases)
provide Ubuntu 26.04 amd64 packages and checksums. These packages depend on that
distribution's libraries; ARM64 artifacts are currently suspended. To build for
your installed distribution:

```sh
sudo apt-get install dpkg-dev file
cmake -S . -B build/package -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_SYSCONFDIR=/etc -DUSE_GIT_VERSION=OFF
cmake --build build/package --parallel 2
ctest --test-dir build/package --no-tests=error --output-on-failure
cpack --config build/package/CPackConfig.cmake -B build/package/packages
sudo apt install ./build/package/packages/shairport-2_*.deb
```

Packages install both binaries, service units, manual, sample configuration,
README, changelog and license/provenance notices. They neither activate services
nor migrate older installations. Packaged binaries use `/usr/bin`; source
installs default to `/usr/local/bin`.

## Configure and play

Edit `/etc/shairport-2.conf` using the [sample](scripts/shairport-2.conf).
Set `general.name` for the advertised name and optionally `pulseaudio.server`
and `pulseaudio.sink` for the audio destination. Validate before activation:

```sh
shairport-2 --version
shairport-2 --check-config --config /etc/shairport-2.conf
sudo systemctl daemon-reload
sudo systemctl enable --now shairport-2-nqptp
systemctl --user daemon-reload
systemctl --user enable --now shairport-2
```

Avahi and the user's PulseAudio-compatible session must be running. Select the
receiver in your device's AirPlay output picker and start playback. Restart with
`systemctl --user restart shairport-2` after configuration changes or a timing
companion restart.

The foreground CLI accepts only no arguments, `--config PATH`, `--version`, and
`--check-config [--config PATH]`. Version/configuration checks work without
audio, discovery or timing services. An absent default configuration uses
built-in defaults; an explicitly missing file fails. Invalid arguments exit 2;
configuration/startup errors exit 1. SIGINT and SIGTERM request shutdown.

Legacy flags belong in configuration: `--name` → `general.name`, `--port` →
`general.port`, `--password` → `general.password`, `--verbose` →
`diagnostics.log_verbosity`, `--statistics` → `diagnostics.statistics`.
`--configfile`/`-c` and `-V` are replaced by `--config` and `--version`.
Daemon/PID/control, backend-selection and syslog flags are unsupported.
The sample and [manual](man/shairport-2.1) describe supported runtime settings.
Removed backend groups, including empty groups, are errors. Interpolation
accepts `auto`, `basic` or `vernier`; `soxr` is unsupported.

For source installs, `./user-service-install.sh --dry-run` previews user-unit
installation; `./user-service-install.sh` installs and starts it for the current
audio user after the companion and Avahi are running. Adjust its default
`/usr/local/bin/shairport-2` path for another prefix. Do not run it as root.

### Upgrade older installations

1. Inspect and stop old user/system `shairport-sync`, `shairport-sync-nqptp`
   and standalone `nqptp` units, including manually launched processes. Preserve
   custom units/drop-ins for adapting to the new names.
2. Back up the old configuration and copy it without overwriting an existing
   destination to `/etc/shairport-2.conf` (or your installation directory).
   The old default file is not loaded automatically. Update explicit `--config`
   paths and compare settings with the sample. Keep `general.name` and the
   device id stable; update an old `pulseaudio.application_name` if necessary.
3. Remove the old Debian package without purging its configuration, then install
   the new one. For source installs, inspect manifests before removing obsolete
   files. Update wrappers, monitoring and user-unit overrides with old executable
   paths. Validate configuration and matching receiver/companion `--version`
   output before activation.
4. Start the companion first, then the receiver. Verify playback, reconnection
   and Home pairing on real devices. Pairing records are in-memory.

Timing companions share UDP 319/320, loopback UDP 9000 and `/nqptp` shared
memory; run only one. Startup rejects socket conflicts and an existing shared
memory object. After an unclean termination, confirm all owners have stopped
before removing stale `/dev/shm/nqptp`. Normal shutdown removes the companion's
own object. Restart the receiver after every companion restart to remap it.
System and user managers have no automatic dependency between these services.

## Troubleshooting and logs

Logs go to stderr, captured by the service journal. Collect evidence with:

```sh
journalctl --user -u shairport-2 --since '30 minutes ago' --no-pager
sudo journalctl -u shairport-2-nqptp --since '30 minutes ago' --no-pager
```

Invalid tagged metadata warns at normal verbosity with connection id,
SET_PARAMETER operation, body length and response status. Nearby unknown
parameter diagnostics summarize counts/categories without dumping values or
headers. For additional diagnostics set `diagnostics.log_verbosity = 1` in the
configuration and restart; `diagnostics.statistics = "yes"` adds playback
statistics. Higher verbosity can include protocol content in other existing
diagnostics: inspect logs before sharing.

| Symptom | Checks |
| --- | --- |
| Startup fails before discovery | Validate configuration; inspect companion status/journal and readable, initialized SMI10 shared memory. Check timing conflicts above. |
| Receiver absent from picker | Check Avahi, Bonjour multicast filtering and network isolation. RTSP defaults to TCP 7000; session UDP ports start at 6001. |
| Connected but silent | Run `pactl info` and `pactl list short sinks` in the audio user's session. Check sink, volume and `pipewire-pulse`. |
| Dropouts/timing problems | Inspect packet loss, clock availability, output latency and playback statistics. |
| Home/pairing problems | Keep name/device id stable; allow Bonjour discovery. Use Home → Add Accessory → More Options. Check restart/reconnect and removal/re-addition on real devices. |

Report receiver/sender versions, topology, stream format, output layout,
reproduction steps and the relevant time window of logs.

## Develop and validate

Production code lives in `src/`, tests in `tests/`, the timing companion in
`companion/nqptp/`, and the embedded pairing dependency in `pair_ap/`. Components
own protocol decisions and resources; native effects belong in adapters.
Pairing lifetime contracts are documented at
[PairingSession](src/session/pairing_session.hpp) and
[EncryptedChannel](src/transport/encrypted_channel.hpp).
Source authorship/license notices remain in the pairing library.

For short feedback cycles, build a relevant target and select named scenarios:

```sh
cmake --build build/cmake --target rtsp-parameters-test --parallel 2
ctest --test-dir build/cmake -R '^RtspParameters\.' --no-tests=error --output-on-failure
ctest --test-dir build/cmake -N
```

CI runs Release, ASan+UBSan and TSan on AMD64; Release also checks staged
installation and Debian packaging. The [workflow](.github/workflows/receiver.yml)
contains exact sanitizer configuration commands. Compiler-rt must be installed
for sanitizer builds. Some cancellation tests use `tests/cancellation_wait.hpp`
under TSan and do not establish native blocked-read cancellation safety. The
test-only NQPTP preload fixture is uninstrumented; production objects are
instrumented. Infrastructure failures do not prove race freedom.

Automated checks do not establish real-device playback quality, Home, live
re-pairing, multiroom synchronization or controlled performance. Repeated
pairing setup must be serialized against active channel IO; whole-process
cooperative shutdown and remaining runtime/global-state encapsulation are
incomplete. On a test host, verify realtime/buffered ALAC, AAC stereo/5.1/7.1,
volume/mute, pause/resume, flush, reconnect, Home add/remove and shutdown/restart
during IO. Package tests do not activate host services.

Open small pull requests with concrete improvements and verification evidence.
Use English for repository work. Conventional Commits determine releases:
breaking changes require major, `feat` minor, `fix`/`perf` patch; other types do
not release by themselves. The largest bump wins. Automation updates `VERSION`,
[CHANGELOG.md](CHANGELOG.md), tags and tested artifacts after green CI;
do not increment versions manually. Release-tool checks run with
`npm ci --prefix .github/release` and `npm test --prefix .github/release`.

Licensing and attribution are in [LICENSES](LICENSES), [COPYING](COPYING), source
notices and [timing companion provenance](companion/nqptp/PROVENANCE.md).

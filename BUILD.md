# Build and install

## CMake / C++26 build

Use CMake 4.2 or newer, Ninja, Clang 23.1.3 (pinned in `.tool-versions`)
and libstdc++ 15. The matching `clang-scan-deps` must be installed beside Clang;
the toolchain requires it for named C++ modules. CMake checks C++26 without GNU extensions by compiling and
running `cmake/cpp26_probe.cpp`. All receiver production sources, including the
entrypoint and generated plist, compile as C++26. The bundled NQPTP 1.2.8
companion also builds from committed C++26 source with the same version and
toolchain; see [its provenance](companion/nqptp/PROVENANCE.md).
Companion dependencies are Linux/POSIX sockets and shared memory, threads and
the realtime system library, declared separately from receiver dependencies.
The bundled pairing dependency
remains C behind an explicit linkage boundary; C tests exercise the receiver APIs.
CMake rejects any own production source configured to compile as C.

Install the asdf toolchain from the repository root:

```sh
sudo apt-get install cmake ninja-build g++-15 g++ python3
asdf plugin add clang https://github.com/higebu/asdf-llvm.git
git -C "${ASDF_DATA_DIR:-$HOME/.asdf}/plugins/clang" checkout b7b8dd389c790237145e7436f1cd85b49611e37e
asdf install
asdf reshim clang
```

The asdf plugin builds LLVM from source. CI uses a checksum-verified official
binary archive; see the workflow in `.github/workflows`.
The toolchain resolves compiler paths through `asdf which` from the repository.
Install the receiver dependencies on Debian/Ubuntu, then build:

```sh
sudo apt-get install pkg-config libgtest-dev libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

Only Linux with AirPlay 2, PulseAudio, Avahi, FFmpeg and OpenSSL is supported.
Provider switches are rejected. `INSTALL_CONFIG_FILES=OFF` disables installing
the sample configuration. CMake installs the binary under the chosen prefix,
the manual under its `share/man/man1` directory, and the sample under
`CMAKE_INSTALL_SYSCONFDIR`.
Generated configuration, plist and Git version files live in the CMake build
directory. Use an out-of-tree build; remove any stale `config.h` from the source
directory before configuring. To install on the host after checking the staged
files, run `sudo cmake --install build/cmake`.

## Build checks

Test builds require GoogleTest 1.17 or newer (`libgtest-dev`). Production-only
builds configured with `-DBUILD_TESTING=OFF` do not require GoogleTest.
All C++ suites register named scenarios with CTest. For a short feedback cycle,
build the relevant executable and select one case:

```sh
cmake --build build/cmake --target volume-policy-test --parallel 2
ctest --test-dir build/cmake -R '^VolumePolicy.MuteLevelRequestsMute$' --no-tests=error --output-on-failure
```

Use `ctest --test-dir build/cmake -N` to list cases, or a suite prefix such as
`-R '^RtpClock\.'` to select a group. Keep `--no-tests=error` so a mistyped name
fails. Full builds and CTest runs still cover the C adapters, linkage and shell
contracts. Policy, clock, audio-format/mapping, PCM/samples and playback-timing
targets use separate libraries; unrelated receiver changes do not relink their
test executables. This improves targeted feedback rather than clean-build time.
The [architecture guide](docs/architecture.md) also maps independent RTSP,
buffered/control, transport, pairing and session targets to their dependencies.
For example, directory policy tests link without receiver or crypto libraries,
while the pairing session declares its native crypto dependencies:

```sh
cmake --build build/cmake --target pairing-directory-test pairing-session-test --parallel 2
ctest --test-dir build/cmake -R '^(PairingDirectory|PairingSession.*)\.' --no-tests=error --output-on-failure
```

Module scanning is enabled on module targets and importing translation units.
Do not substitute a `clang-scan-deps` from another LLVM version. Validation scope
and native-cancellation limitations are recorded in the
[architecture guide](docs/architecture.md#validation).

CI runs three native AMD64 CMake builds: Release, ASan+UBSan and TSan,
all with the same pinned compiler and library. Every build runs its contracts,
including companion timing, lifecycle and real socket/receiver-reader tests,
while only Release stages both binaries, units, licenses, manual and sample
configuration, verifies installation, builds the Debian package and uploads it
without starting a service. The sanitizer jobs check instrumentation in receiver
and companion runtime/packet-handler C++ object files. TSan
detects data races between threads; ASan detects memory misuse and UBSan detects
undefined behavior. All three builds and release-automation checks must pass
before a release can publish.
ARM64 CI and release artifacts are suspended for now.
Failed tests upload CTest diagnostics, and an empty test selection fails CI.

The compiler installation must include compiler-rt runtimes for sanitizer builds;
the official binary archive used by CI includes them. Some source-built asdf
installations contain only Clang and headers and must install compiler-rt first.
Reproduce ASan+UBSan separately from TSan:

```sh
cmake -S . -B build/asan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
cmake --build build/asan --parallel 2
ctest --test-dir build/asan --output-on-failure
cmake -S . -B build/tsan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=thread -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer'
cmake --build build/tsan --parallel 2
ctest --test-dir build/tsan --output-on-failure
```

The NQPTP fixtures preload an uninstrumented test-only shared library built by
the system `cc`; receiver code remains instrumented with Clang's static sanitizer
runtime. Expected startup failures still reject sanitizer diagnostics, including
leaks. Leak detection remains enabled. A TSan runtime startup failure caused by
host address-space or sandbox restrictions is failed infrastructure validation;
it does not establish the absence of data races.

Run the bundled `shairport-2-nqptp` timing companion (SMI10) and Avahi as
system services. Start a PulseAudio-compatible user session. PipeWire users
should run `pipewire-pulse`; the native PipeWire backend is not provided.
Installation ships both units without enabling or starting them. Copy and
configure the sample as described below before activation. Activate the
companion first, then the receiver from the audio user's session:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now shairport-2-nqptp
systemctl --user daemon-reload
systemctl --user enable --now shairport-2
```

The system companion uses a dynamic user, `CAP_NET_BIND_SERVICE` and the
upstream realtime-priority limit of 6. It keeps production UDP ports 319/320,
loopback UDP 9000 and the `/nqptp` shared-memory object. There is no dependency
between the system and user systemd managers. The receiver remains in the
PulseAudio-compatible user's session. Avahi must also be running before the
receiver starts.

### Migrating from shairport-sync

The project is now `shairport-2`, at
<https://github.com/gstn-caruso/shairport-2>. The receiver, timing companion,
Debian package and service units have new names. Configuration defaults to
`/etc/shairport-2.conf` in Debian packages; the old configuration is not read
automatically. The AirPlay model identifier, timing ports
and `/nqptp` shared-memory interface retain their compatibility identities.

1. Inspect the old installation before changing it. Run
   `systemctl --user status shairport-sync` as each audio-session owner and
   `sudo systemctl status shairport-sync shairport-sync-nqptp nqptp` for system
   units. Stop and disable the old units that exist; for the previous bundled
   installation, use `systemctl --user disable --now shairport-sync` and
   `sudo systemctl disable --now shairport-sync-nqptp`. Stop any manually
   launched receiver or timing companion too. An older system receiver uses
   `sudo systemctl disable --now shairport-sync`. Keep custom units and
   drop-ins for reference; the new service names need their own configuration.
2. Preserve the existing configuration. For the packaged default, run
   `sudo cp --archive --no-clobber /etc/shairport-sync.conf /etc/shairport-sync.conf.pre-shairport-2`
   and `sudo cp --archive --no-clobber /etc/shairport-sync.conf /etc/shairport-2.conf`.
   Inspect an existing destination instead of overwriting it. Source installs
   may use `/usr/local/etc`; use the actual old and new configuration directories.
   If the old unit used `--config`, migrate that file and update the new unit's
   explicit path. Pairing records are in-memory; verify reconnection and Home
   pairing manually after the upgrade.
3. For Debian installations, inspect `dpkg-query -W shairport-sync`, remove
   the old package with `sudo apt remove shairport-sync` after stopping its
   services, then install the new `shairport-2_*.deb` as described below.
   Use removal without purge to preserve local configuration. Source installs
   should stage and inspect the new files before installing; review the old
   build's install manifest before removing obsolete files. The package does
   not migrate configuration, remove old source-installed binaries or activate
   services. Update command wrappers, monitoring and custom unit paths from
   `shairport-sync` to `shairport-2` and the companion likewise.
4. Compare the preserved configuration with `scripts/shairport-2.conf` and
   follow [configuration migration](CONFIGURATION.md). Run
   `shairport-2 --check-config --config /etc/shairport-2.conf` before activation.
   Set `pulseaudio.application_name = "shairport-2"` if the copied configuration
   retains the previous display label. Keep `general.name` to preserve the
   receiver name shown to senders. Run `shairport-2 --version` and
   `shairport-2-nqptp --version` to verify matching receiver release versions.
5. Reload both systemd managers, start `shairport-2-nqptp` first, then enable
   `shairport-2` in the audio user's session using the commands above. Check
   both journals and manually verify AirPlay playback. Do not run the old and
   new timing companions together: their sockets and shared memory overlap.

If the checkout directory changed, create a fresh CMake build directory. Old
CMake caches contain absolute paths to the previous checkout. Update local
shortcuts and Git remotes as needed; do not reuse a stale build or install manifest
as evidence of the new installation.

### Migrating a separately installed NQPTP

Before activating the bundled companion, stop and disable the existing NQPTP
service (normally `sudo systemctl disable --now nqptp`). Inspect its actual unit
name and stop any manually started copy too. The package does not modify or
disable an existing NQPTP or PTP service. Conflicts on any required supported
socket address fail startup with a clear diagnostic; the companion does not
continue with a partial binding.

Shared-memory creation is exclusive: an existing `/nqptp` object is preserved
and startup fails rather than overwriting it. Normal shutdown removes the
companion's own object. After an unclean termination, confirm that all NQPTP
processes and any service owning it are stopped before manually removing the
stale `/dev/shm/nqptp` object, then start the companion again. Check
`sudo journalctl -u shairport-2-nqptp` for binding or mapping failures.

After every companion restart, restart the receiver with
`systemctl --user restart shairport-2` so it remaps shared memory. The old
mapping can retain an obsolete clock even after its object is unlinked.
Real AirPlay playback, multiroom synchronization and privileged activation
remain manual acceptance checks on a designated test host; isolated tests
verify sockets, timing samples and mapping behavior only.

Copy `shairport-2.conf.sample` to `shairport-2.conf` and edit it before
starting the receiver. When migrating an existing configuration, see
[CONFIGURATION.md](CONFIGURATION.md) for unsupported settings.

From the repository root, `./user-service-install.sh --dry-run` previews user service installation. `./user-service-install.sh` installs and starts the unit for your current user. Its default executable path is `/usr/local/bin/shairport-2`; edit the unit if you chose another prefix. Do not run the installer as root. NQPTP and Avahi must already be running.

`systemctl --user status shairport-2` and `journalctl --user -u shairport-2` show receiver status and logs. The unit does not create a system account or install a system-wide receiver service.

## Debian packages and releases

Every CI Release build uploads a `.deb` and checksum as workflow artifacts,
including changes that do not increase the version. Packages built by CI target
Ubuntu 26.04 amd64 and depend on that distribution's runtime libraries; they are
not universal packages for older Ubuntu or Debian releases. CPack derives the
library dependencies with `dpkg-shlibdeps`. The package also declares Avahi,
systemd and a PulseAudio-compatible provider (`pulseaudio | pipewire-pulse`).
It includes the companion and both service units, without automatic activation.
ARM64 packages and native ARM test execution are currently suspended.

Build a package locally with the installed distribution's dependencies:

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

The package contains `/usr/bin/shairport-2` and
`/usr/bin/shairport-2-nqptp`, both service units, the manual, license notices,
the README, build/configuration/architecture guides, generated changelog and
`/etc/shairport-2.conf.sample`. The documentation bundle is installed under
`/usr/share/doc/shairport-2`; its build examples and source navigation assume
a repository checkout. Copy the
sample to `/etc/shairport-2.conf` and configure it before starting. The
packaged system unit uses `/usr/bin/shairport-2-nqptp`; activate it first with
`sudo systemctl enable --now shairport-2-nqptp`. The packaged user unit uses
`/usr/bin/shairport-2`; enable it with `systemctl --user enable --now shairport-2`
from the PulseAudio user's session after starting the companion and Avahi.
Companion provenance and GPLv2 notices are installed under
`/usr/share/doc/shairport-2/nqptp/`. Remove any older user-unit override that still
points to `/usr/local/bin/shairport-2`.

Pushes to `master` or `main`, including merged PRs, run the same validation.
Before native builds, semantic-release performs a dry run to select the version
from commits since the last release. After all checks pass, it publishes that
same version using the tested packages:

| Commit type | Version change |
| --- | --- |
| Any type with `!` or a `BREAKING CHANGE:` footer | Major |
| `feat` | Minor |
| `fix`, `perf` | Patch |
| All other commits, including `docs`, `test`, `refactor`, `style`, `chore`, `build`, `ci` | No release by themselves |

Merges publish a release only when the unreleased commit range requires a bump;
an empty range does not publish again. The largest required bump wins. Preserve
that meaning in squash-merge titles
and bodies. `VERSION` is the CMake version source. For a release, automation
updates it with `CHANGELOG.md`, commits the
release metadata, creates `v<version>` and publishes a GitHub Release with
the amd64 `.deb` file and `SHA256SUMS`. AMD64 builds
and tests the planned version before publication. The publisher requires exactly
one amd64 package with matching package name, version and
architecture metadata. Release binaries use the package version instead of a
Git description. Release jobs are serialized; superseded runs skip publication
so a later validated push covers their commits.

Do not increment `VERSION` manually. Release tooling is pinned in
`.github/release/package-lock.json`; run its checks with
`npm ci --prefix .github/release` and `npm test --prefix .github/release`.

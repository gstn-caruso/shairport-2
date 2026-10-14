# shairport-2

[![CI](https://github.com/gstn-caruso/shairport-2/actions/workflows/receiver.yml/badge.svg)](https://github.com/gstn-caruso/shairport-2/actions/workflows/receiver.yml)
[![Release](https://img.shields.io/github/v/release/gstn-caruso/shairport-2)](https://github.com/gstn-caruso/shairport-2/releases)
[![License](https://img.shields.io/badge/license-MIT%20%2B%20GPLv2-blue)](LICENSES)

An open-source AirPlay 2 audio receiver for Linux. Select it on your AirPlay
device to play through your Linux machine's speakers.

Supports realtime and buffered playback, encrypted pairing, sender-controlled
volume, and ALAC/AAC stereo and surround streams. Requires PulseAudio or
PipeWire's `pipewire-pulse`, Avahi, and the bundled NQPTP timing companion.
AirPlay 1 and other audio backends are unsupported.

## Install

[Releases](https://github.com/gstn-caruso/shairport-2/releases) provide Ubuntu
26.04 amd64 Debian packages and checksums. Download a package, then install it:

```sh
sudo apt install ./shairport-2_*.deb
```

Packages include the receiver, timing companion, service units, manual and
sample configuration. Services are not started automatically. For other Linux
distributions, build from source.

### Build from source

Requires Meson 1.10+, Ninja, Clang 23.1.3, libstdc++ 15 and GoogleTest 1.17+.
The commands below also install the Ruby and Debian tools used for package
creation and installation checks. From the repository root:

```sh
sudo apt-get install meson ninja-build g++-15 g++ ruby dpkg-dev systemd
asdf plugin add clang https://github.com/higebu/asdf-llvm.git
git -C "${ASDF_DATA_DIR:-$HOME/.asdf}/plugins/clang" checkout b7b8dd389c790237145e7436f1cd85b49611e37e
asdf install
asdf reshim clang
sudo apt-get install pkg-config libgtest-dev libpopt-dev libconfig-dev libpulse-dev libavahi-client-dev libssl-dev libplist-dev libplist-utils libsodium-dev libgcrypt20-dev uuid-dev libavutil-dev libavcodec-dev libavformat-dev libswresample-dev xxd
CC="$(asdf which clang)" CXX="$(asdf which clang++)" \
  meson setup build/meson --buildtype=release --prefix=/usr --sysconfdir=/etc \
  -Duse_git_version=false
meson compile -C build/meson -j 2
meson test -C build/meson --print-errorlogs --num-processes 1
meson install -C build/meson --destdir "$PWD/build/meson/stage"
ruby tests/install_contract_test.rb "$PWD/build/meson/stage" /usr "$(cat VERSION)"
```

The asdf plugin builds LLVM from source. Resolve the compiler paths with
`asdf which` so builds outside the checkout also use the pinned compiler.
Use a fresh build directory after moving a checkout, and remove stale
source-tree `config.h` before configuring. `-Dtests=false` skips GoogleTest;
`-Dinstall_config_files=false` omits the sample configuration. Git revision
information is enabled by default; the commands above disable it for a release
build. Inspect the staged files, then install:

```sh
sudo meson install -C build/meson
```

Meson's default prefix is `/usr/local`; the commands above select `/usr` with
configuration under `/etc`, as required for Debian packages. Build a package
from the same configuration:

```sh
meson compile -C build/meson deb
(cd build/meson/packages && sha256sum --check *.sha256)
```

The package and checksum are written to `build/meson/packages/`. The `deb`
target is available when Ruby, `dpkg`, `dpkg-deb` and `dpkg-shlibdeps` are
installed at configure time. After adding these tools, run
`meson setup --reconfigure build/meson`. See the
[CI workflow](.github/workflows/receiver.yml) for sanitizer build commands and
package extraction checks.

## Configure and play

Create a configuration without overwriting an existing one:

```sh
sudo cp --no-clobber /etc/shairport-2.conf.sample /etc/shairport-2.conf
```

Edit `/etc/shairport-2.conf`: set `general.name` for the advertised name and,
optionally, `pulseaudio.server` and `pulseaudio.sink` for the audio destination.
See the [sample configuration](scripts/shairport-2.conf) and
[manual](man/shairport-2.1) for supported settings and CLI options.

Ensure Avahi and the audio user's PulseAudio-compatible session are running,
then validate the configuration and start the services:

```sh
shairport-2 --check-config --config /etc/shairport-2.conf
sudo systemctl daemon-reload
sudo systemctl enable --now shairport-2-nqptp
systemctl --user daemon-reload
systemctl --user enable --now shairport-2
```

The receiver runs as a **user service** in the audio user's session; the timing
companion runs as a system service. Select the receiver in your device's AirPlay
output picker and start playback.

Restart with `systemctl --user restart shairport-2` after configuration changes
or **every timing companion restart**. Run only one timing companion: instances
share UDP 319/320, loopback UDP 9000 and `/nqptp` shared memory. After an unclean
shutdown, confirm all owners have stopped before removing stale `/dev/shm/nqptp`.

For source installs, `./user-service-install.sh --dry-run` previews user-service
setup; run it without `--dry-run` to install and start the receiver. Run as the
audio user, after starting Avahi and the companion. Adjust the script's binary
path if you changed the installation prefix.

### Upgrading from shairport-sync

Stop old `shairport-sync`, `shairport-sync-nqptp` and standalone `nqptp` services
and processes. Back up the old configuration and adapt it to the current sample
at `/etc/shairport-2.conf`; the old file is not loaded automatically. Keep the
receiver name and device id stable, and update custom units and executable paths.
Remove the old package without purging its configuration before installing this
one. Validate the configuration, start the companion before the receiver, and
check playback and pairing. Pairing records are in-memory.

## Troubleshooting

```sh
journalctl --user -u shairport-2 --since '30 minutes ago' --no-pager
sudo journalctl -u shairport-2-nqptp --since '30 minutes ago' --no-pager
```

| Problem | Check |
| --- | --- |
| Startup fails | Validate configuration and inspect the companion journal for timing conflicts. |
| Receiver not listed | Check Avahi, Bonjour multicast and network isolation. RTSP defaults to TCP 7000; session UDP ports start at 6001. |
| Connected but silent | Run `pactl info` and `pactl list short sinks` as the audio user; check sink, volume and `pipewire-pulse`. |
| Dropouts | Check packet loss, clock availability and output latency. Enable `diagnostics.statistics = "yes"`. |
| Home pairing fails | Keep name/device id stable; try Home → Add Accessory → More Options. |

For more detail, set `diagnostics.log_verbosity = 1` and restart. Inspect logs
before sharing: higher verbosity can include protocol content. Report versions,
network setup, stream format, reproduction steps and relevant logs.

## Development

Production C++26 code lives in `src/`, tests in `tests/`, the timing companion in
`companion/nqptp/`, and the embedded pairing library in `pair_ap/`.

CI runs Release, ASan+UBSan and TSan on AMD64, including installation and package
checks. Real-device playback, Home pairing and multiroom synchronization need
manual validation. Re-pairing during active IO and cooperative shutdown remain
limitations.

Use English and Conventional Commits. Open small PRs with verification evidence.
Release automation manages versions, [changelogs](CHANGELOG.md) and artifacts;
do not increment versions manually.

## License

See [LICENSES](LICENSES), [COPYING](COPYING), source notices and
[timing companion provenance](companion/nqptp/PROVENANCE.md).

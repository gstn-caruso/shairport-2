# Configuration and troubleshooting

Version 6.0.0 replaced the legacy CLI with the foreground interface below.
Version 6.1.0 bundled the timing companion. Stop an older standalone timing
service before activating it and restart the receiver after a companion restart;
see [timing migration](BUILD.md#migrating-a-separately-installed-nqptp).
Both applications share one version; [CHANGELOG.md](CHANGELOG.md) records releases.
For the rename from `shairport-sync` to `shairport-2`, follow the
[program migration guide](BUILD.md#migrating-from-shairport-sync) before
activating the new services. The previous default configuration is not loaded
automatically.

## Foreground operation

The operational interface accepts only no arguments, `--config PATH`,
`--version`, and `--check-config [--config PATH]`. Version reporting and
configuration validation work without running PulseAudio, Avahi or NQPTP.
No arguments use `shairport-2.conf` in the installation configuration
directory (`/etc` in Debian packages); an absent default file uses built-in
defaults. Explicit missing or unreadable files fail. Invalid arguments exit 2;
configuration or startup failures exit 1.

Run under the systemd user service. Logs go to stderr for the journal. Restart
after configuration changes. SIGTERM and SIGINT request shutdown. The listener,
activity monitor, event worker and buffered TCP transport have owned cooperative
stop paths; other workers still use cancellation. Whole-process cooperative
shutdown remains migration work; see [the lifecycle map](docs/architecture.md#resource-retirement).

Legacy setting flags are removed. Move supported settings to libconfig:

| Former flag | Configuration equivalent |
| --- | --- |
| `--name` | `general.name` |
| `--port` | `general.port` |
| `--password` | `general.password` |
| `--stuffing` | `general.interpolation` |
| `--statistics` | `diagnostics.statistics` |
| `--verbose` | `diagnostics.log_verbosity` |
| `--logOutputLevel` | `diagnostics.log_output_level` |
| `--on-start`, `--on-stop` | `sessioncontrol.run_this_before_play_begins`, `sessioncontrol.run_this_after_play_ends` |
| `--wait-cmd` | `sessioncontrol.wait_for_completion` |
| `--timeout` | `sessioncontrol.session_timeout` |
| `--latency` | `latencies.default` (deprecated; prefer `general.audio_backend_latency_offset_in_seconds`) |
| Deprecated `--resync`, `--tolerance` | `general.resync_threshold_in_seconds`, `general.drift_tolerance_in_seconds` |

Use `--config PATH` instead of `-c` or `--configfile`; `--version` replaces `-V`.
Daemon/PID/control, backend-selection and syslog flags are unsupported. Use
`journalctl --user -u shairport-2` for diagnostics.

## Build stack

The CMake build is AirPlay 2 on Linux using PulseAudio, Avahi, OpenSSL and FFmpeg. These dependencies cannot be disabled. Other platforms fail explicitly.

Use `-DCMAKE_INSTALL_PREFIX=/usr/local` and `-DCMAKE_INSTALL_SYSCONFDIR=/etc` to choose installation paths. `-DINSTALL_CONFIG_FILES=OFF` skips sample configuration installation. See [BUILD.md](BUILD.md) for the pinned compiler and build commands.

Legacy `WITH_`, `WITHOUT_` and `CONFIG_` CMake cache switches fail explicitly: audio/discovery alternatives, alternate ALAC decoders, daemon/PID support, DSP/SoX interpolation, metadata export, DBus, MPRIS, MQTT and system/init installers. The sample configuration documents the current runtime settings.

Runtime backend selection (`--output`, `--mdns`, `general.output_backend`, `general.mdns_backend`) and service selection (`--service-type`, `general.service_type`) are removed. Use `pulseaudio.server` and `pulseaudio.sink` to select the audio destination. Interpolation accepts `auto`, `basic` or `vernier`; `soxr` fails.

## Troubleshooting

Check `shairport-2 --version` and validate the effective configuration with
`shairport-2 --check-config --config /etc/shairport-2.conf`. Both commands
work without audio, discovery or timing services. Inspect receiver logs with
`journalctl --user -u shairport-2`; increase `diagnostics.log_verbosity` and
optionally enable `diagnostics.statistics` for playback problems.

| Symptom | Checks |
| --- | --- |
| Startup fails before discovery | Check `sudo systemctl status shairport-2-nqptp`, its journal, readable shared memory and matching SMI10. Follow the timing migration guide for port conflicts or stale shared memory. |
| Receiver is absent from the picker | Check Avahi, Bonjour multicast filtering and network isolation. The RTSP port defaults to TCP 7000; negotiated session UDP ports start at 6001 by default. |
| Connected but silent | Run `pactl info` and `pactl list short sinks` in the audio user's session; check server, sink, volume and `pipewire-pulse` availability. |
| Configuration rejected | Compare with `scripts/shairport-2.conf`. Removed options and even empty legacy backend groups are errors. |
| Dropouts or timing problems | Enable diagnostics and inspect packet loss, clock availability and output latency. |

### Home and pairing

Keep the receiver name and device identifier stable. `general.airplay_device_id`
or its offset can distinguish multiple receivers. Sender and receiver need a
network that permits Bonjour discovery. In Home, choose Add Accessory → More
Options, select the receiver and follow the pairing flow.

The management directory is in-memory, not persistent pairing storage. Validate
pairing, restart/reconnect and removal/re-addition with actual devices. Repeated
setup versus active event IO has an inherited cipher synchronization gap; local
protocol tests do not establish live re-pairing safety. Record receiver/sender
versions, stream format and logs when reporting problems. See the architecture
guide for [validation limits](docs/architecture.md#validation).

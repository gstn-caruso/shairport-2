# Build configuration

## Foreground operation

The operational interface accepts only no arguments, `--config PATH`,
`--version`, and `--check-config [--config PATH]`. Version reporting and
configuration validation work without running PulseAudio, Avahi or NQPTP.
No arguments use `shairport-sync.conf` in the installation configuration
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
`journalctl --user -u shairport-sync` for diagnostics.

`ConfigurationLoader` returns owned settings using explicit environment inputs.
`ReceiverApplication` retains the process-lifetime compatibility lease for
runtime consumers that still read global configuration. Shared timing settings,
the pairing directory and immutable device-ID/PIN options are already passed
explicitly through the listener to sessions. These boundaries do not imply
complete resource/session encapsulation; see [migration status](docs/receiver-migration-progress.md).
The popt library remains solely for existing subprocess-hook argument splitting;
operational arguments use `StartupOptions`.

## Build stack

The CMake build is AirPlay 2 on Linux using PulseAudio, Avahi, OpenSSL and FFmpeg. These dependencies cannot be disabled. Other platforms fail explicitly.

Use `-DCMAKE_INSTALL_PREFIX=/usr/local` and `-DCMAKE_INSTALL_SYSCONFDIR=/etc` to choose installation paths. `-DINSTALL_CONFIG_FILES=OFF` skips sample configuration installation. See [BUILD.md](BUILD.md) for the pinned compiler and build commands.

Legacy `WITH_`, `WITHOUT_` and `CONFIG_` CMake cache switches fail explicitly: audio/discovery alternatives, alternate ALAC decoders, daemon/PID support, DSP/SoX interpolation, metadata export, DBus, MPRIS, MQTT and system/init installers. The sample configuration documents the current runtime settings.

Runtime backend selection (`--output`, `--mdns`, `general.output_backend`, `general.mdns_backend`) and service selection (`--service-type`, `general.service_type`) are removed. Use `pulseaudio.server` and `pulseaudio.sink` to select the audio destination. Interpolation accepts `auto`, `basic` or `vernier`; `soxr` fails.

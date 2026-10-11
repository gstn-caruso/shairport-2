# Troubleshooting

Check `shairport-sync --version` for AirPlay2, OpenSSL, Avahi and PulseAudio.
Validate the effective file with `shairport-sync --check-config --config /etc/shairport-sync.conf`.
Both commands work without running audio/discovery/timing services. The supported
CLI is documented in [CONFIGURATION.md](CONFIGURATION.md) and `man shairport-sync`;
legacy `-V`, `-h` and setting flags are rejected.

A timing startup error means the receiver will not listen or advertise. Check
`sudo systemctl status shairport-sync-nqptp` and
`sudo journalctl -u shairport-sync-nqptp`, readable shared memory and matching
SMI10. For port conflicts or stale shared memory, follow
[timing migration](BUILD.md#migrating-a-separately-installed-nqptp).
Restart the receiver after restarting the companion so it remaps the object.

For audio, inspect `pactl info` and `pactl list short sinks` from the same user session. Set `pulseaudio.server` or `pulseaudio.sink` if needed. PipeWire is used via `pipewire-pulse`.

For discovery, check Avahi, network isolation and multicast filtering. The default RTSP port is TCP 7000; session UDP ports default to the configured range starting at 6001. Keep the sender and receiver reachable over IPv4 or IPv6.

Removed settings are errors, including empty legacy groups such as `alsa`, `pipewire`, `dsp`, `metadata`, `dbus`, `mpris` and `mqtt`. Migrate existing configuration to `scripts/shairport-sync.conf`. Interpolation supports `auto`, `basic` and `vernier`.

Use `journalctl --user -u shairport-sync` for logs. Increase `diagnostics.log_verbosity` and optionally enable statistics while diagnosing realtime or buffered playback. Verify volume, pause/resume, reconnect and multiroom synchronisation with actual AirPlay 2 senders.

Report the binary version, sender/device versions, stream format and relevant
logs. Local native pairing tests do not establish live re-pairing or Home
acceptance. The inherited repeated-setup/event-cipher synchronization gap and
remaining shutdown limits are listed in [VALIDATION.md](VALIDATION.md).

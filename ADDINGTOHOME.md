# Add the receiver to Home

Run the receiver in a PulseAudio-compatible user session with Avahi and compatible NQPTP already running. The receiver and iPhone/iPad must be on a network that permits Bonjour discovery.

In Home, choose Add Accessory, then More Options, and select the advertised receiver name. Follow the pairing flow. The AirPlay 2 pairing, verification and accessory configuration endpoints are retained in this fork.

Use a stable receiver name and device identifier. `general.airplay_device_id` or its offset can distinguish multiple receivers. Pairing, reconnecting after a restart and removal/re-addition in Home require validation with actual devices; automated protocol tests do not prove Home integration end to end.

The management directory is in-memory and is not persistent pairing storage.
Native authorization behavior is unchanged. Repeated setup versus active event
IO still has an inherited synchronization gap; a successful local exchange is
not evidence that live re-pairing is safe. Record the receiver and sender
versions for device results and consult [VALIDATION.md](VALIDATION.md).

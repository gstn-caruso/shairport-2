# Shairport Sync — AirPlay 2 receiver for Linux

Turn a Linux machine and its speakers into an AirPlay 2 audio destination.
Select the receiver on an AirPlay device; Shairport Sync receives the encrypted
stream, decodes it, schedules playback against the AirPlay clock and sends the
audio to your PulseAudio-compatible session.

This fork provides realtime and buffered playback, encrypted pairing, Home
endpoints and sender-controlled volume. It runs as a **systemd user service**
and works with PulseAudio or PipeWire's `pipewire-pulse` compatibility server.

This repository now maintains both the **audio receiver** and its **timing
companion**. The companion tracks the sender's clock in a separate system
service; both applications are built, tested and packaged together, use C++26
and share one release version. No separate timing-project checkout is needed.

The supported stack is Linux, AirPlay 2, PulseAudio, Avahi, OpenSSL and FFmpeg,
with the bundled timing companion. The pairing dependency remains C. AirPlay 1
and alternative audio or discovery backends are not supported.

## Start here

- [How the services fit together](#how-the-services-fit-together)
- [From discovery to playback](#from-discovery-to-playback)
- [How audio reaches the speakers](#how-audio-reaches-the-speakers)
- [How playback stays synchronized](#how-playback-stays-synchronized)
- [Build and install](#build-and-install)
- [Configure and start](#configure-and-start)
- [Find your way around the source](#find-your-way-around-the-source)
- [Validation and troubleshooting](#validation-and-troubleshooting)

## How the services fit together

Shairport Sync handles the AirPlay session and playback. Three services provide
discovery, clock information and access to the audio device:

```mermaid
flowchart TD
    sender["AirPlay device"]
    subgraph system["Linux system services"]
        avahi["Avahi: Bonjour discovery"]
        timing["Timing companion: PTP clock tracking"]
    end
    subgraph user["Linux audio user's session"]
        receiver["Shairport Sync"]
        audio["PulseAudio or pipewire-pulse"]
    end
    speakers["Audio device and speakers"]
    sender -->|"Discover receiver"| avahi
    sender -->|"PTP timing traffic"| timing
    sender -->|"Session control and encrypted audio"| receiver
    receiver -.->|"Publish receiver records"| avahi
    timing -->|"Clock information via shared memory"| receiver
    receiver -->|"PCM audio"| audio
    audio --> speakers
```

| Component | Responsibility |
| --- | --- |
| **Avahi** | Publishes Bonjour records so AirPlay devices can find the receiver. |
| **Timing companion** | Tracks the AirPlay PTP clock and exposes its relationship to the local clock through shared memory. |
| **Shairport Sync** | Pairs with senders, manages sessions, receives and decodes audio, controls volume and schedules playback. |
| **PulseAudio / pipewire-pulse** | Negotiates the output format and channel layout, queues PCM audio and routes it to a sink. |

Avahi and the timing companion must be running before the receiver starts.
Installation ships both the companion's system unit and the receiver's user
unit without enabling or starting them. Shairport Sync runs as the user who
owns the audio session; Avahi remains a separately installed system service.

Avahi advertises `_airplay._tcp` and a complementary `_raop._tcp` record for
AirPlay 2 discovery. The RAOP record does not enable AirPlay 1 playback.

## From discovery to playback

Discovery makes the receiver visible. Session control then establishes the
encrypted connection, audio transport and timing information needed to play.
This is a conceptual flow; individual senders can vary the order and number of
protocol exchanges.

```mermaid
sequenceDiagram
    participant S as AirPlay device
    participant A as Avahi
    participant R as Shairport Sync
    participant N as Timing companion
    participant P as PulseAudio / pipewire-pulse
    R->>N: Validate shared-memory interface at startup
    R->>A: Publish discovery records
    S->>A: Discover receiver
    A-->>S: Receiver identity and endpoint
    S->>R: Connect to RTSP endpoint (default TCP 7000)
    S->>R: Pair / verify encrypted session as needed
    S->>R: SETUP: negotiate stream and timing
    S->>R: Send audio and playback timing updates
    N-->>R: Shared-memory clock readings
    R->>R: Decrypt, decode, buffer and schedule
    R->>P: Submit PCM for playback
    loop While the session is active
        S->>R: Audio, volume and playback-control updates
        R->>P: Scheduled audio with volume applied
    end
    S->>R: Flush or teardown when requested
    R->>R: Reset queued playback or release session resources
```

The RTSP handler supports setup, record, teardown, pairing and Home endpoints,
rate/anchor updates, flush operations and parameter handling. An **anchor**
connects an audio frame timestamp to a time on the shared AirPlay clock.
Volume commands update playback gain; a flush discards or resets affected
queued playback, while teardown releases the session.

To add the receiver to Home, use **Add Accessory → More Options**, then select
its advertised name. Keep the receiver name and device identifier stable.
See [ADDINGTOHOME.md](ADDINGTOHOME.md) for pairing guidance and device checks.

## How audio reaches the speakers

The sender chooses a stream type during setup. Both paths ultimately use the
same decoder, playback queue and PulseAudio output backend.

| Playback mode | Transport | Codec | Input format |
| --- | --- | --- | --- |
| Realtime | UDP audio packets | ALAC | 44.1 kHz / 16-bit stereo |
| Buffered | TCP buffered audio | ALAC | 48 kHz / 24-bit stereo |
| Buffered | TCP buffered audio | AAC | Stereo, 5.1 and 7.1 |

```mermaid
flowchart TD
    sender["Encrypted audio from the sender"]
    realtime["Realtime: UDP packets"]
    buffered["Buffered: TCP reader and encoded-audio buffer"]
    decrypt["Authenticate and decrypt payloads"]
    decode["FFmpeg: ALAC or AAC decoding"]
    queue["Decoded audio packet queue"]
    convert["Resampling and output channel mapping"]
    pcm["PCM encoding, software gain and timing corrections"]
    output["PulseAudio backend"]
    speakers["Selected audio sink and speakers"]
    timing["Playback timing and synchronization decisions"]
    volume["Sender volume commands and volume policy"]
    sender --> realtime
    sender --> buffered
    realtime --> decrypt
    buffered --> decrypt
    decrypt --> decode --> queue --> convert --> pcm --> output --> speakers
    timing -.->|"When to release audio"| queue
    timing -.->|"Drift correction"| pcm
    volume -.->|"Gain and mute"| pcm
```

**Realtime playback** processes incoming packets and tracks sequence gaps.
Packet-buffer and retransmission logic can request missing packets while there
is still time to use them.

**Buffered playback** reads an encoded stream ahead over TCP. The buffered
processor uses playback timing to decide when to decrypt and pass blocks into
the player, and handles buffered flushes and timestamp discontinuities.

After decoding, audio waits in the packet queue until playback is due. The
player converts samples to the negotiated output rate and layout, applies
software volume and synchronization corrections, and submits PCM to the
backend. **PCM** is the uncompressed sample representation delivered to the
audio server; the input format does not guarantee the final hardware format.

## How playback stays synchronized

Packet arrival time is not playback time: network delay varies, and the audio
server already has samples queued. The receiver combines sender timestamps,
clock information and output latency to schedule playback:

```mermaid
flowchart TD
    anchor["Sender anchor: audio frame + AirPlay clock time"]
    clock["Timing companion: AirPlay clock relative to local clock"]
    rtp["RTP clock mapping"]
    target["Target playback time on the local clock"]
    delay["PulseAudio latency measurement"]
    policy["Playback timing and sync policy"]
    action["Wait, preroll silence, release or correct audio"]
    anchor --> rtp
    clock --> rtp
    rtp --> target --> policy
    delay --> policy
    policy --> action
```

1. **Translate the timeline.** RTP timestamps identify positions in the audio
   stream. Sender anchors and companion clock information map those positions to
   local playback times.
2. **Account for queued output.** The backend reports latency, allowing
   playback timing to plan initial silence and release audio ahead of its
   intended audible time.
3. **Correct drift.** Playback synchronization compares expected frame timing
   with output-delay observations. It can adjust samples, insert silence or
   discard audio when resynchronization is needed. Tolerances and interpolation
   are configurable in the sample configuration.

The timing companion supplies clock information, not audio. It exposes **shared-memory
interface version 10**. Missing, inaccessible, uninitialized, truncated or
incompatible shared memory stops startup before the receiver listens or
advertises. Passing that startup check does not by itself establish an active
sender clock or good multiroom synchronization.

## Build and install

Use CMake **4.2 or newer**, Ninja, the pinned **Clang 23.1.3** toolchain in
[.tool-versions](.tool-versions), and **libstdc++ 15**. Test builds require
GoogleTest 1.17 or newer. Follow [BUILD.md](BUILD.md) to install the toolchain and
development dependencies before running these commands from the repository
root:

```sh
cmake -S . -B build/cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_SYSCONFDIR=/etc
cmake --build build/cmake --parallel 2
ctest --test-dir build/cmake --output-on-failure
DESTDIR="$PWD/build/cmake/stage" cmake --install build/cmake
```

Inspect both staged binaries, service units, licenses, manual and configuration under
`build/cmake/stage` before installing on the host:

```sh
sudo cmake --install build/cmake
sudo cp --no-clobber /etc/shairport-sync.conf.sample /etc/shairport-sync.conf
```

These commands use the default `/usr/local` prefix and put the sample
configuration in `/etc`. The installed service units use the selected prefix.
See [BUILD.md](BUILD.md#debian-packages-and-releases) for Debian packaging and
the supported distribution; packages also contain both applications and units.

## Configure and start

Edit `/etc/shairport-sync.conf`. Start with the
[sample configuration](scripts/shairport-sync.conf), which documents naming,
volume, synchronization, session and diagnostic settings.

- `general.name` sets the name shown to AirPlay devices; `%H` uses the hostname.
- `pulseaudio.server` selects the PulseAudio-compatible server.
- `pulseaudio.sink` selects an output sink; leaving it unset uses the default.
- `diagnostics.log_verbosity` and `diagnostics.statistics` enable more detail
  when investigating playback.

See [CONFIGURATION.md](CONFIGURATION.md) before migrating an older configuration:
legacy backend selectors and several upstream options have been removed.

Before starting, make sure Avahi is running and the current user has a
PulseAudio-compatible audio session. PipeWire users need `pipewire-pulse`.
The network must permit Bonjour discovery and negotiated AirPlay traffic;
the RTSP listener uses **TCP port 7000** by default, with additional audio and
control ports negotiated during setup.

If you previously installed a standalone timing service, stop and disable it
before activating the bundled companion. Both use the same timing ports and
shared-memory object. Follow the
[migration instructions](BUILD.md#migrating-a-separately-installed-nqptp),
including checks for manually started processes and stale shared memory.

Start the timing companion first, then enable the receiver as the user who owns
the audio session. The companion's current executable and system unit are named
`shairport-sync-nqptp`; use that exact name in commands:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now shairport-sync-nqptp
systemctl --user daemon-reload
systemctl --user enable --now shairport-sync
systemctl --user status shairport-sync
journalctl --user -u shairport-sync
```

The companion needs UDP ports **319/320**, loopback UDP **9000** and the `/nqptp`
shared-memory object. Its system unit supplies the privilege needed to bind
the timing ports. Inspect its logs with
`sudo journalctl -u shairport-sync-nqptp`.

The receiver starts with the user's `default.target` and restarts on failure.
Its startup depends on the user's audio session. After every companion restart,
also run `systemctl --user restart shairport-sync` to remap shared memory;
systemd does not coordinate dependencies between the system and user managers.

Select the receiver in your device's AirPlay output picker and start playback.
After changing configuration, restart the receiver:

```sh
systemctl --user restart shairport-sync
```

## Find your way around the source

Production sources are grouped by responsibility under [`src/`](src).
Each package's `CMakeLists.txt` owns its source list. Root
[CMakeLists.txt](CMakeLists.txt) owns toolchain checks, dependencies, generated
files, tests and installation.

| Package | What it owns |
| --- | --- |
| [`src/app`](src/app) | Owned configuration, startup checks and application composition. |
| [`src/discovery`](src/discovery) | Bonjour records and Avahi registration. |
| [`src/protocol/rtsp`](src/protocol/rtsp) | Framing/parameter components, runtime requests, pairing endpoints and stream setup. |
| [`src/protocol/rtp`](src/protocol/rtp) | Realtime packet reception, control traffic and clock anchors. |
| [`src/protocol/ap2`](src/protocol/ap2) | Buffered/event components, pairing directory and native management adapters. |
| [`src/session`](src/session) | Registry/runtime workers, principal policy, per-session state and native pairing ownership. |
| [`src/transport`](src/transport) | Bounded byte queues, native transport lifetime, event workers and encrypted byte IO. |
| [`src/audio`](src/audio) | Formats, channel mapping, decoding, buffering, resampling, PCM and output adapters. |
| [`src/packets`](src/packets) | Retransmission planning. |
| [`src/playback`](src/playback) | Player coordination, playback-thread lifecycle, timing, synchronization and statistics. |
| [`src/timing`](src/timing) | RTP clock mapping and companion shared-memory access. |
| [`src/volume`](src/volume) | Volume policy, session volume state and runtime effects. |
| [`src/monitoring`](src/monitoring) | Receiver activity state and monitoring. |
| [`src/runtime`](src/runtime), [`src/platform/utilities`](src/platform/utilities) | Shared runtime support and platform/protocol utilities. |
| [`pair_ap`](pair_ap) | Bundled C pairing implementation. |
| [Timing companion](companion/nqptp) | PTP clock tracking, socket and shared-memory ownership; see its [upstream provenance](companion/nqptp/PROVENANCE.md). |

Independent targets now cover session/principal policy, RTSP framing/parameters,
buffered/realtime/control reception, transport, pairing, audio conversion/output,
timing, volume and text. Native adapters still contribute to the shared
`receiver` target. Ten named C++ modules isolate policy and protocol boundaries;
ordinary headers remain at native IO and C ABI boundaries.

[The architecture guide](docs/architecture.md) maps each component to its
responsibilities and dependencies. [Migration status](docs/receiver-migration-progress.md)
distinguishes merged work from remaining global configuration, public session
state, worker shutdown and re-pairing synchronization. The migration is not
complete. See [BUILD.md](BUILD.md#build-checks) for isolated feedback commands.

## Validation and troubleshooting

[CI](.github/workflows/receiver.yml) builds Release, Debug, ASan+UBSan and TSan
configurations, runs CTest contracts and checks staged installation. Tests cover
decoding, buffering, timing, volume, session lifecycle, RTSP handling and startup
validation, plus companion timing, lifecycle and socket/shared-memory integration.
[VALIDATION.md](VALIDATION.md) records coverage and reproducible
device checks.

Automated tests do not establish playback quality or multiroom timing. Actual
AirPlay devices are needed to validate playback, pairing, reconnect and Home
integration end to end.

| Symptom | First checks |
| --- | --- |
| Receiver fails before becoming visible | Read the user-service log; check timing-companion availability and shared-memory compatibility. |
| Timing companion fails to start | Read its system-service log; check for an existing timing service, port conflicts or stale shared memory using the migration guide. |
| Service runs but receiver is absent from the picker | Check Avahi, receiver naming and whether the network permits Bonjour discovery. |
| Receiver connects but produces no sound | Check the audio user's PulseAudio / `pipewire-pulse` session and the configured server and sink. |
| Dropouts or timing problems | Enable diagnostics; check packet loss, clock information and output latency. |
| Pairing or Home problems | Follow [ADDINGTOHOME.md](ADDINGTOHOME.md) and inspect receiver logs. |

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for further diagnostics and
[AIRPLAY2.md](AIRPLAY2.md) for protocol details.

## Documentation and licenses

- [BUILD.md](BUILD.md): dependencies, installation, targeted tests and sanitizer builds.
- [CONFIGURATION.md](CONFIGURATION.md): build options and configuration migration.
- [AIRPLAY2.md](AIRPLAY2.md): supported protocol behavior and stream formats.
- [ADDINGTOHOME.md](ADDINGTOHOME.md): Home pairing and device checks.
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md): diagnostic guidance.
- [VALIDATION.md](VALIDATION.md): test coverage, evidence and verification limits.
- [Architecture](docs/architecture.md): component ownership, modules and explicit dependencies.
- [Migration status](docs/receiver-migration-progress.md): delivered boundaries and remaining work.
- [RELEASENOTES.md](RELEASENOTES.md): fork release history and upgrade guidance.

All project documentation, code comments, commit messages and pull requests
must be written in English. The diagrams use Mermaid, which GitHub renders
directly in this README.

Licenses are listed in [LICENSES](LICENSES) and the source files.
Historical upstream notes and earlier implementation evidence are retained under
[`docs/history`](docs/history); their commands and feature lists describe those
old revisions, rather than the current supported stack.
The documentation bundle is staged with the program and installed under
`share/doc/shairport-sync`. Build examples and source-navigation links assume
a repository checkout.

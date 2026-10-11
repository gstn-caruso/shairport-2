# Receiver migration contracts

This is the Stage 0 inventory for the responsibility-driven C++26 migration.
It describes source commit `dbe218e4422ee31343d44a5aaaf0ab55a0d7bf63`, verified
on 2026-10-10. Existing characterization is evidence of particular outcomes,
not proof of complete AirPlay interoperability. Pending checks below are gates
for the affected migration slices.

See [migration progress](receiver-migration-progress.md) for subsequent
deliveries and their verification; the observations below describe the original
baseline.
For current component boundaries and validation use
[architecture](architecture.md) and [VALIDATION.md](../VALIDATION.md).
The CLI and ownership gaps in this inventory are historical observations, not
instructions to restore removed options or evidence that merged work is pending.

## Verification baseline

Expectation: a freshly configured and rebuilt Release receiver still passes all
260 registered tests, and every subsystem has an observable contract plus an
explicit coverage gap. Before this change there was no tracked migration
contract inventory at this path. No production behavior changes in this slice.

The baseline ran in the original checkout at the source commit above, before
writing this document in the separate documentation worktree:

```sh
cmake -S . -B build/redesign-final-release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/redesign-final-release -j 4
/usr/bin/time -f 'elapsed=%e user=%U sys=%S maxrss_kib=%M' \
  ctest --test-dir build/redesign-final-release --output-on-failure
```

The existing out-of-tree Ninja cache was checked for this repository's source
path, Release, `BUILD_TESTING=ON`, and both Clang 23.1.3 compiler paths under
asdf. CMake 4.2.3 configured successfully and the build rebuilt 141 steps.
Re-specifying the toolchain emitted an unused-variable warning because the
compiler was already selected in this cache; compiler validation and the C++26
probe still passed. Bundled pairing and legacy receiver code emitted compiler
warnings; this inventory does not resolve them.

Result: **260/260 passed**, CTest real time **16.11 s**. The enclosing time
measurement was elapsed **16.12 s**, user **6.52 s**, system **4.23 s**, maximum
resident set **209440 KiB** on x86_64. This is one serial test-run observation
including child processes and the configure-contract test, not receiver
throughput, live receiver memory, processing latency, allocation count, or
underrun performance. Host load and cache state were not controlled. Repeat the
same command/configuration for feedback timing; obtain live playback baselines
separately before making performance claims.

Only Release was rerun locally for this inventory. CI currently defines four
AMD64 configurations: Release, Debug, ASan+UBSan, and TSan. ARM CI and release
artifacts are suspended at the user's request. `BUILD.md` contained older
dual-architecture statements at this baseline; this documentation slice aligns
those statements with the current workflow.

## Contract ownership and coverage

Paths in the following table are relative to the repository root. Test file
names refer to `tests/`; their cases are registered through `CMakeLists.txt`.
The owners are intended responsibility boundaries, evolving existing objects
where they already exist, rather than a requirement to create a class per row.

| Subsystem and owner | Observable contract to preserve or introduce | Current evidence | Verification gap before changing this subsystem |
|---|---|---|---|
| Operational interface — `StartupOptions`, configuration loader, `ReceiverApplication` | Foreground execution under the user service; default/explicit configuration; version and validation without external services; predictable exit statuses; stderr logging; signal-driven shutdown. The desired interface is detailed below. | `src/app/main.cpp` delegates to `shairport_receiver_main`; `src/app/shairport.cpp` has legacy two-pass popt parsing. `removed_options_test.sh` rejects already removed flags. | No dedicated `--check-config`, argument/status matrix, missing explicit file rejection, isolated settings loader, service-independent validation, successful startup cleanup, or process SIGTERM acceptance scenario. |
| Configuration — loader producing immutable `ReceiverSettings` | Preserve supported libconfig syntax, defaults, setting values, and explicitly rejected removed settings. An absent default file uses defaults; explicit unreadable/missing input fails. Restart applies changes. | `src/app/shairport.cpp`, `scripts/shairport-sync.conf`, `CONFIGURATION.md`; `removed_options_test.sh` covers removed groups/settings and interpolation selection. | Source currently ignores failed `realpath` for both default and explicit paths. Inventory each legacy setting flag against configuration before deleting CLI handling; verify ranges, wrong types, precedence, unreadable files, and effective default/explicit results without services. |
| RTSP framing — message/parser | Preserve method/path interpretation, header lookup, duplicate ordering, binary bodies, response framing, and owned input lifetime. Preserve deliberate rejection/truncation boundaries until separately changed. | `src/protocol/rtsp/rtsp_message.*`; `rtsp_message_test.cpp` and C adapter test cover parsing, malformed lines, first case-insensitive duplicate, field/header limits, oversized response, exact serialized responses, canceled partial input, plist release. | Add deterministic fragmented read/write transcripts, malformed/negative/oversized content lengths, disconnect at each boundary, multiple requests, encrypted framing, bounded parser fuzzing after contracts are fixed. |
| RTSP interpretation — focused handlers | AirPlay 2 advertised methods; correct response codes, volume/progress updates, metadata acceptance, PTP setup; reject unsupported methods and NTP setup. | `src/protocol/rtsp/rtsp.cpp`; `rtsp_dispatch_test.cpp` checks OPTIONS, volume SET/GET, RECORD, TEARDOWN, rejected GET/POST/FLUSH, ANNOUNCE/PAUSE/unknown, NTP and metadata completeness. | Full successful SETUP/RECORD/FLUSHBUFFERED/TEARDOWN wire exchanges and plist fixtures, exact response headers/bodies, malformed setup/event payloads, reconnect. Current dispatch helpers carry unrelated histories. |
| RTP/AP2 admission — buffer, retransmission planner, reception adapters | Packet ownership, modular sequence arithmetic, loss/retry timing, duplicate/late/overflow behavior, exact flush boundaries, realtime/buffered/event interpretation and decryption. | `src/packets/`, `src/protocol/rtp/`, `src/protocol/ap2/`; `audio_packet_buffer_test.cpp`, `player_packet_test.cpp`, `retransmission_planner_test.cpp`, `audio_packet_wait_test.cpp` cover windows, retries, wraparound, revisions, flushes, frame retention, waits and cancellation cleanup. | Packet/socket transcripts for realtime and buffered receivers, truncation/malformed lengths, encrypted audio and event messages, retransmission wire bytes, reconnect and exact multi-packet flush behavior. Buffer tests alone do not characterize full reception. |
| Session ownership — registry, principal selection, `Session` | Empty registry safety, exclusive principal, replacement, retirement, failed worker creation, repeated stop; workers finish before sockets/session resources close. | `src/session/`, `src/playback/playback_run.*`; `session_registry_test.cpp`, `session_replacement_test.cpp`, `session_shutdown_test.cpp`, `playback_run_test.cpp`, `principal_volume_test.cpp` cover failure ownership, immediate completion, cancellation-safe join, concurrent retirement/replacement and idempotence. | Encapsulation of public mutable `SessionState` remains pending. Preserve outcomes when replacing cancellation mechanics; full listener/session startup races and real disconnect/reconnect remain pending. |
| Concurrency — each worker owner | Required new shutdown order: request stop, wake blocked operations, join, release resources. Stop during accept/read/write and empty/full queue waits must complete reliably. | Current registry/playback and protocol workers use pthreads and cancellation; wait/shutdown tests exercise current cancellation cleanup. | Those tests are characterization of cleanup outcomes, not cooperative-stop acceptance. Listener, RTP/AP2, monitoring, PulseAudio output workers, blocked writes, full queues and process signals need stop/wakeup tests with explicit synchronization. |
| Volume — policy, control, principal, adapters | RTSP volume response, mute, fixed-point gain, profiles, rounding/range boundaries, remembered principal volume and ordered hardware/software/hook effects. | `src/volume/`; `volume_policy_test.cpp`, `volume_control_test.cpp`, `volume_adapter_test.cpp`, `volume_transaction_test.cpp`, `principal_volume_test.cpp`, `volume_command_cancel_test.cpp`, `player_volume_wait_test.cpp`. Policy covers fractional truncation, hardware priority/range fallbacks and mute; transaction tests cover concurrent setter/startup. | Complete typed RTSP-volume slice with explicit failures and quantities; invalid wire value matrix, response/effect ordering at the request boundary, cooperative hook termination/reaping without forced cancellation. |
| Decode/resample/channel mapping — format, decoder, resampler | Supported ALAC/AAC stereo/surround shapes, valid output negotiation, frame lifetime, channel order, exact conversion bytes and retained-frame continuity; failures preserve owned state. | `src/audio/decoding/`, `src/audio/resampling/`, `src/audio/format/`; `audio_decoder_test.cpp` checks format preparation/error/resource paths and silent ALAC encode/decode. `resampler_test.cpp` has non-silent per-channel synthetic samples, mapping, negotiation rejection and direct FFmpeg continuity comparison. `audio_format_test.cpp`, `channel_mapping_test.cpp`, `audio_input_state_test.cpp` check shape/mapping/state. | Non-silent ALAC/AAC compressed golden fixtures for every supported shape, decoded PCM expectations and corrupt packets; AAC surround end-to-end decode and sink negotiation. Existing synthetic resampler tests do not establish compressed playback fidelity. |
| PCM/sample transformations — samples, encoder | Signed sample bytes/endian formats, clipping, gain, mono/stereo/surround order, stuffing corrections, silence duration, dither policy/seed continuity. | `src/audio/pcm/`; `pcm_encoder_test.cpp`, `playback_samples_test.cpp`, `converted_audio_test.cpp`, `receiver_encoding_cpp_test.cpp`, player packet/volume-wait tests include non-silent positive/negative/extreme samples and encoded handoff. | Full decode-to-sink golden bytes across reconfiguration/flush, continuous non-silent dither reference, supported real-device surround channels and underrun behavior. Keep simple processing loops when they express the algorithm. |
| Timing/synchronization — RTP clock, playback timing/sync, NQPTP adapter | Explicit clock domains and RTP wraparound; scheduling/preroll/flush/resync thresholds, retained frames, absent/replaced anchors; NQPTP SMI version 10 with no classic fallback. | `src/timing/`, `src/playback/timing/`; `rtp_clock_test.cpp`, `playback_timing_test.cpp`, `playback_sync_test.cpp`, `playback_statistics_test.cpp`, `player_volume_wait_test.cpp` cover calculation boundaries, wraparound and missing anchor waits. `nqptp_test.sh` preloads missing/permission/version/truncated/inconsistent shared-memory failures. | Successful live shared-memory updates, anchor replacement during reception, daemon-independent config/version paths, multiroom drift/latency and timing measurements. Fixture startup failures do not prove real NQPTP integration. |
| Pairing — state progression, TLV codecs, crypto resource owners | HomeKit/Fruit setup/verify state progression, authenticated bytes, key derivation inputs, algorithms/parameters, nonce/counter progression/rollback, cipher framing and secret cleanup must remain identical. | Four C units in `pair_ap/`, fixed libgcrypt/libsodium boundary in `pair_ap/README.md`; `pair_cipher_bundle_test.cpp` covers buffer/description-before-cipher release and empty repeated release. | Deterministic TLV fragmentation/truncation vectors, setup/verify transcripts, invalid proofs/tags/states, frame limits/counters/rollback, differential tests, all error cleanup paths, real pairing/re-pairing and Home integration. Bundle resource tests are not protocol-vector coverage. |
| Output/discovery — PulseAudio/Avahi adapters | PulseAudio user-session access, format/channel negotiation, volume/output failure handling; advertised AirPlay identifiers/features, discovery updates/removal. | `src/audio/output/audio_pa.cpp`, `audio.*`, `audio_player_adapter.hpp`; `src/discovery/`; resampler fake backend checks negotiation/mapping and receiver string/encoding tests check representations. | Real PulseAudio connection/failure/drain/shutdown, Avahi registration/collision/reconnect/update/removal transcripts, meaningful fake boundaries. Output still uses a function-pointer table; native service types belong at adapter boundaries. |
| Activity/utilities/hooks/logging — state and platform adapters | Activity expiration/reactivation/stop; subprocess exit, termination and reaping; stable identity/random services, network/file operations and journal-visible logs. | `src/monitoring/`, `src/platform/utilities/`, `src/runtime/common.*`; `activity_state_test.cpp`, `activity_monitor_test.c`, `structured_buffer*_test.*`, `string_utilities*_test.*`, `statistics_formatter_test.cpp` and cancellation hook tests. | Success/failure hook lifecycle with cooperative termination, logging routing/error cases, network/file failure cleanup and identity persistence. Remaining global/common responsibilities need explicit dependencies; utility tests do not cover complete OS effects. |
| Build/install/release — CMake and package adapters | Pinned C++26 toolchain, fixed Linux/AirPlay 2/PulseAudio/Avahi/FFmpeg stack, genuine C ABI boundaries, binary/manual/sample configuration/user service, `/etc` package configuration location, semantic release meaning. | `CMakeLists.txt`, `cmake/`, `tests/cmake_configure_test.sh`, `.github/workflows/receiver.yml`, `.github/release/*test.mjs`; CMake rejects own receiver production C and removed stack switches. `scripts/shairport-sync.user.service` runs foreground with restart-on-failure; package rewrites the binary prefix. | Pairing is excluded from the production C++ guard and C compilation remains. Check final CXX-only targets/instrumentation, staged package paths/version/manual/settings, service shutdown and installed-device operation. AMD64 only for now; no ARM acceptance claim. |

## Intended daemon interface versus current behavior

The legacy CLI is deliberately outside the compatibility contract. Stage 2 is
a breaking change and needs a `BREAKING CHANGE` footer plus migration guidance
from supported setting flags to configuration equivalents.

| Invocation or outcome | Required behavior | Baseline difference |
|---|---|---|
| No arguments | Start with installation configuration directory plus `shairport-sync.conf`; `/etc/shairport-sync.conf` in Debian packages. Missing default uses built-in defaults. | Default basename is derived from executable name in `shairport.cpp`; explicit and default missing paths share ignored `realpath` failure. |
| `--config PATH` | Start using that file; missing/unreadable explicit file fails. | Current name is `-c`/`--configfile`, and missing explicit paths may proceed to startup. |
| `--version` | Existing version representation; success without PulseAudio, Avahi or NQPTP. | Already handled early when first argument; extra arguments are not fully validated on this path. |
| `--check-config [--config PATH]` | Load/validate effective settings and exit without starting network/audio services. | Not implemented; settings processing currently reaches service startup. |
| Invalid arguments | Exit 2, including removed legacy flags. | popt/`die` handling must be replaced and statuses characterized. Existing removed-option tests intentionally need updating. |
| Configuration/startup error | Exit 1 with diagnostics on stderr and cleanup of acquired resources. | Failure fixtures exist for NQPTP and removed settings, not the full startup/cleanup matrix. |
| SIGTERM/SIGINT | Cooperative stop/wakeup/join/resource release. Systemd owns background operation/restart/supervision. | Signal handlers and cancellation-based cleanup exist; process-level cooperative shutdown remains pending. |

The systemd user unit retains PulseAudio session access. Do not introduce
self-forking, PID files, live configuration reload, or a separate control app.
Remove popt only after no production/test/build consumer needs it. Update the
service, shell tests, manual, sample configuration, guide and package checks
together with this interface.

## Stage 1 assessment

The GoogleTest registration part is already implemented: CMake uses
`gtest_discover_tests`, per-case names and timeouts, and separate calculation
libraries. Named formats are explicit tests, for example ALAC/AAC stereo and
surround cases. Preserve those useful names and the existing C adapter checks.

Expression work remains. At this baseline `rg 'assert\(' tests -g '*.cpp'`
finds 360 raw assertions across 18 of 36 C++ test files; Release test targets
keep assertions active with `-UNDEBUG`, but failures still lack GoogleTest
actual/expected diagnostics. Searching C++ tests for `PrintTo`, `TEST_P` and
`INSTANTIATE_TEST_SUITE_P` finds no matches. Named explicit tests are valid;
parameterization is useful only when it makes essential inputs clearer.

The first useful expression slice is `rtsp_dispatch_test.cpp`: replace opaque
assertions with observable response/value checks and split the shared-method,
OPTIONS, NTP and metadata scenarios. `checkAirplay2Methods` currently invokes
shared volume/progress history; `checkRejectedNtpSetup` invokes that history;
the metadata test invokes rejected NTP setup. Those histories hide the actual
scenario inputs. Similar candidate histories exist in decoder/resampler helper
checks. Add value printers when domain values otherwise have poor diagnostics,
and retain the exact behavioral expectations while changing test structure.
Stage 1 is therefore **partially implemented, not complete**.

## Pending acceptance and gate

Stage 0's inventory gate is met: every planned subsystem above has identified
observable contracts, current evidence and explicit verification gaps. This
gate does not mean every characterization fixture or device check exists.

Before finishing the migration, capture representative full wire exchanges,
deterministic non-silent compressed audio and pairing transcripts. Run device
checks for realtime/buffered playback, surround formats, pause/resume,
reconnect, Home integration, pairing/re-pairing and multiroom timing. Record
CPU, live memory, processing latency, allocations and underruns under the same
source material, device topology, sample format and load before/after migration.
No such device or receiver-performance baseline was obtained in this slice;
those checks remain pending.

The design criterion is **God Service (#78)**: move decisions to objects that
own the relevant information, keeping application/protocol coordination small.
This inventory changes documentation only; no structural tidying was needed.
Manual verification consists of checking each table row against its linked
source/test paths, confirming the measured test result, and ensuring desired
behavior is explicitly distinguished from current implementation.

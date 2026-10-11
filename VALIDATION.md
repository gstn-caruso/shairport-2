# Validation

This records the verified **v6.1.16** state on **2026-10-11**, not completion of
the [component migration](docs/receiver-migration-progress.md).
Use [BUILD.md](BUILD.md) for fresh build and sanitizer configurations, and
[architecture](docs/architecture.md) for dependency and ownership boundaries.

## Latest evidence

| Check | Verified result | Scope |
| --- | --- | --- |
| Local Release | 695/695 CTest entries | Full receiver/companion suite after PR71's functional changes |
| Local ASan+UBSan / TSan | 49/49 focused cases in each | Pairing session, native adapters, listener/options lifetime and event teardown |
| Independent review | 37/37 selected cases in each of Release, ASan+UBSan and TSan | Native context/result ownership, replies, options and cleanup |
| Hosted CI | Release, Debug, ASan+UBSan, TSan and release-policy passed | PR71 and post-merge source `1a06ea54942ba9b9592269128f1ae68650d2082b` |
| Release | v6.1.16 tag, changelog and AMD64 package verified | Checksum, package version/architecture, extracted binary `/etc` configuration path and sample-config validation |

[Post-merge run 38131299979](https://github.com/gstn-caruso/shairport-sync/actions/runs/38131299979)
completed successfully, including publication. The latest release is
[v6.1.16](https://github.com/gstn-caruso/shairport-sync/releases/tag/v6.1.16).
Package verification used extraction; it did not install or restart services.
Counts are a source snapshot, not line/branch coverage or a future fixed test count.

The documentation refresh also checked version/config validation without
services, the updated named selection (15 available pairing/directory cases),
manual rendering, and the staged companion/package contract. The architecture
guide and manual in the staging tree match the source files. Staging is not a
host installation. The expanded installed-documentation bundle applies to
subsequent source/CI builds; published v6.1.16 assets are unchanged.

## Contract coverage

| Boundary | Representative observable checks |
| --- | --- |
| CLI/configuration | No-service version/check-config; argument/status matrix; explicit file errors, owned settings and removed settings |
| Session/principal | Admission, replacement, retirement, value initialization, exact shared owners and failed-start cleanup |
| RTSP | Fragmented framing, binary bodies, response bytes, parameter/volume effects and setup responses |
| Buffered/realtime/control | Authenticated payloads, packet/flush boundaries, clock anchors, resend bytes and descriptor retirement |
| Event transport | Exact binary-plist commands, full-capacity replies, descriptor zero, blocked native read/write stop and destruction order |
| Pairing | Ordered management TLVs; legitimate transient/PIN/verify exchanges; status/partial-cipher failures; private result lifetime and seeded data preparation |
| Audio/output | Non-silent sample assertions, decoder/resampler ownership, channel mapping, PCM bytes, output negotiation and queue behavior |
| Companion | Upstream timing oracles, malformed datagrams, isolated sockets/shared memory, restart and receiver-reader integration |
| Build/package | C++26/compiler/module contracts, native dependencies, C compatibility, sanitizer instrumentation and staged installation |

Independent component link commands are part of the evidence: std-only policy
and directory tests exclude the receiver/backend bundle; native codec/pairing
components declare only their actual native dependencies. A green receiver-linked
integration test alone does not prove dependency isolation.

From a configured build, use named selections and fail on empty selections:

```sh
cmake --build build/cmake --target pairing-directory-test pairing-session-test --parallel 2
ctest --test-dir build/cmake -R '^(PairingDirectory|PairingSession.*)\.' --no-tests=error --output-on-failure
ctest --test-dir build/cmake --no-tests=error --output-on-failure
```

The test list is authoritative: `ctest --test-dir build/cmake -N`.
Do not interpret one run's duration as a performance comparison.

## Sanitizer and lifecycle limits

Production objects must contain the sanitizer instrumentation, and sanitizer
diagnostics remain failures. Expected startup errors are not exemptions.
The NQPTP preload fixture is intentionally uninstrumented; the receiver and
companion runtime objects remain instrumented.

Some legacy pthread-cancellation fixtures use a condition-wait seam under TSan
because LLVM 23.1.3 cannot reliably observe their blocking-call unwind. Those
results do not establish native blocked-read cancellation under TSan. See
[the historical experiment](docs/history/receiver-validation-log.md#tsan-cancellation-fixtures)
and `tests/cancellation_wait.hpp`. Actual cooperative event read/write shutdown
and active-session destruction pass through native IO without that seam.

Normal event cleanup is stop/wakeup/join before cipher/context release. It does
not close the inherited repeated-setup versus active-event cipher replacement
gap. Other workers and whole-process cooperative shutdown also remain open;
see [the lifecycle map](docs/architecture.md#resource-retirement).

## Device and performance acceptance

Automated local transcripts do not prove playback quality, Home behavior or
multiroom synchronization. On a designated test host, record the binary version,
sender/device versions, topology, sample material, output layout and observations
for each of these checks:

- Realtime ALAC 44.1 kHz / 16-bit stereo.
- Buffered ALAC 48 kHz / 24-bit stereo and AAC stereo/5.1/7.1.
- Negotiated output, volume/mute, pause/resume, flush and reconnect.
- Pairing/re-pairing, Home add/remove/re-add, and simultaneous multiroom playback.
- Shutdown during accept/read/write and queue waits, followed by restart.

Measure CPU, live resident memory, processing latency, allocations and underruns
before/after under equivalent conditions. No controlled performance baseline or
complete device matrix was obtained in the recent component slices.

Earlier measurements and per-scenario logs are retained in
[the historical validation record](docs/history/receiver-validation-log.md),
[the migration log](docs/history/receiver-migration-log.md), and
[companion verification](companion/nqptp/VERIFICATION.md). They are historical
observations, not evidence that every outstanding acceptance item is complete.

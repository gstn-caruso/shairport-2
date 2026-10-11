# Receiver migration progress

Evidence recorded on 2026-10-10 through source commit
`279c83e2` (activity-monitor slice on the v6.1.0 baseline). The
[initial contract inventory](receiver-migration-contracts.md) remains a
historical baseline. This record distinguishes delivered slices from the
remaining migration and device acceptance work.

## Delivered gates

| Slice | Result and evidence |
|---|---|
| Release prerequisite | PR28 corrected Conventional Commit classification: breaking changes are major, features minor, fixes/performance patch, maintenance alone has no bump. Release-policy tests exercise the real release analysis. |
| ARM suspension | PR29 removed ARM64 CI and release artifacts at the user's request. AMD64 Release, Debug, ASan+UBSan, and TSan remain required. |
| Stage 0 inventory | PR30 identifies contracts and verification gaps for every subsystem. The original Release baseline was 260/260. |
| Stage 1 test expression | PR31, PR33–37, and PR39–41 replace opaque runtime assertions, expose expected values, isolate unrelated histories, and preserve relevant lifecycle transitions. Named RTSP/decoder parameter cases and playback scenario fields retain essential inputs. No raw runtime `assert` calls remain in C++ tests; compile-time assertions and C fixtures are retained. |
| Stage 2 daemon entry point | PR38 shipped the minimal foreground interface in v6.0.0. Configuration/version checks run without services; invalid arguments exit 2 and configuration/startup errors exit 1. Configuration type, conversion, path, latency, and channel-map regressions are covered, together with simulated startup cleanup and SIGTERM/SIGINT during blocked startup. |
| Stage 3 typed volume | PR42 distinguishes AirPlay wire levels, decibels, centibel attenuation, and Q16 PCM gain. Characterization preserves permissive parsing, float rounding, exact mute semantics, invalid-value attenuation, and effect ordering. Primitive conversion remains at RTSP, native output, subprocess, PCM, and C ABI boundaries. |
| Pairing prerequisite | PR43 fixes empty TLV output sizing: an empty value requires its two-byte header before capacity is checked. Seven characterization cases cover empty values, exact/insufficient capacity, mixed values, fragmentation/reassembly, and truncated input. This is not completion of Stage 9. |

The activity-monitor Stage 4 slice gives it ownership of its worker,
synchronization, deadlines, and admitted effects. Its C API remains intact;
`ActivityState` still owns transition decisions. Cooperative stop wakes and
joins the worker, drains admitted effects, and deactivates once. Readiness,
restart, idle/deadline stopping, hook ordering, caller cancellation, exit-handler
ordering, and failed worker creation are tested. Independent review approved
the implementation after a thread-creation exception-safety finding was fixed.
This completes the monitor slice, rather than all Stage 4 workers.

The listener slice now gives `RtspListener` ownership of its worker, stop state,
wake descriptor, and listening sockets. Stop requests wake a blocked poll,
join the worker, and release sockets after the existing session shutdown.
The RTSP loop returns normally; its process-exit handler runs before owner
destruction. Listening sockets are nonblocking, while accepted conversation
sockets remain blocking and close on exec. A stop racing acceptance closes the
accepted descriptor. Failed bind/listen attempts release their descriptors;
failed thread creation leaves the registered exit stop safe. This completes
the listener slice locally, with independent review and hosted CI still pending.
Conversation, playback, and session workers still use cancellation.

The output slice gives `PcmOutputQueue` ownership of bounded PCM occupancy,
whole-frame alignment, wraparound, and FIFO delivery. The named C++ module
`receiver.audio.output.queue` exports its protocol while its storage definition
stays in the implementation unit. Its library depends only on the standard
library; the PulseAudio adapter imports it and owns server reservations,
submission, and uncorking. The first accepted frame is submitted immediately,
and failed writes consume no queued bytes. Zero, null, and subframe reservations
are canceled and retain their PCM for retry. The 12 focused queue/adapter cases
pass in Release and Debug ASan+UBSan; the full pinned Release build
`build/output-components` passes 390/390 cases in 16.80 seconds. The queue cases
link independently of the receiver and PulseAudio. The toolchain packaging
test verifies extraction of the matching `clang-scan-deps`, required by this
module build. Device playback and hosted CI remain acceptance gates; this slice
does not complete Stage 7 audio/timing ownership.

The decoder and resampler slice now builds `receiver-audio-decoder` and
`receiver-audio-resampler` independently. The decoder declares its format,
FFmpeg codec/util, and thread dependencies; the resampler declares its format,
FFmpeg resample/util, and thread dependencies. Their public native types retain
header interfaces with only the required FFmpeg headers. `ConvertedAudio`
requires only FFmpeg memory allocation. Neither component requires receiver
configuration, forced logging headers, or the receiver's native dependency
bundle. Player negotiation and session-boundary tests remain in their own
receiver-linked suite. Fresh standalone builds in `build/audio-components`
pass all 12 decoder cases and 11 resampler cases; their link commands contain
only their component, format, required FFmpeg libraries, and GoogleTest.
The full pinned Release build passes 390/390 cases in 17.22 seconds; all
25 decoder, resampler, and player-boundary cases pass under Debug ASan+UBSan.
This is a dependency boundary within Stage 7, whose broader ownership work
remains open.

The RTSP parameter slice gives message parsing and framing their own standard
library target, `receiver-rtsp-message`. The named C++ module
`receiver.protocol.rtsp.parameters` owns GET/SET parameter responses, content
classification, metadata validation, and wire-volume interpretation. Its
`ParameterVolumePort` explicitly supplies the current level and accepts requested
levels; runtime logging, principal selection, subprocess commands, output effects,
and shared-volume commits stay in the receiver adapter. Accepted volume lines
retain their order and float-rounded permissive parsing. Eight-byte GET volume
prefixes and suffixed content types preserve the existing compatibility rules.
Fresh standalone message/parameter builds in `build/rtsp-components` pass
20/20 cases with only the two protocol libraries and GoogleTest on their link
commands. Socket writes, cancellation, plist logging, C lifetime, dispatch, and
session-volume integration remain receiver-linked tests. This is a protocol
boundary within Stage 6; its broader decomposition remains open.
The full pinned Release build passes 405/405 cases in 16.96 seconds. All
54 focused protocol and adapter cases pass under Debug ASan+UBSan; both new
production implementation objects were checked for sanitizer instrumentation.

The playback output-setup slice gives `OutputSetupCoordinator` owned settings
and an explicit backend port. It chooses the decoded sample format, negotiates
and configures the output, configures the real resampler, publishes successful
resampler state, then configures PCM encoding. The runtime bridge copies borrowed
device channel names and snapshots the configuration's layouts, mixdown, and
mapping names. Cancellation policy, logging, session publication, and fatal
unsupported-PCM handling stay at that boundary. Rejection or resampler failure
keeps prior published/PCM state; an unsupported PCM selection preserves the
existing publication-before-PCM failure sequence. The encoded output ABI's
unchanged bit-layout helpers now have an independent format header.

A fresh standalone build in `build/output-setup-components` passes 12 cases
against real resampler/PCM components, including owned maps, explicit/fallback
decoded formats, rejection, initialization failure, publication ordering, and
surround/mono mixing. Surround samples are compared with directly configured
FFmpeg; the mono oracle verifies normalized matrix weights of 0.5/0.5, producing
7 from input samples 5 and 9. Its link command contains only output setup,
resampler, format, PCM, FFmpeg resample/util, and GoogleTest. Four player-boundary
cases preserve the receiver integration, including missing backend selection
and the existing ignored native configure result. Broader settings/session
ownership and Stage 7 remain open.
The full pinned Release build passes 419/419 cases in 16.94 seconds. All
16 output-setup/player-boundary cases pass under Debug ASan+UBSan; the actual
output-setup implementation object has both sanitizer instrumentations.

The configuration slice replaces the shallow daemon snapshot with move-only
`ReceiverSettings`: owned diagnostics, network/discovery identity, audio and
PulseAudio, volume, and session-hook groups. `ConfigurationLoader` receives
explicit path, host/version, hardware-address, endianness, and interface-index
inputs. Each load owns its libconfig tree and temporary parsing storage; returned
strings, channel names, and ordered diagnostics retain their values after source
removal or moves. Two configurations coexist, and valid/malformed/valid loads do
not reset native logging or share state. Fatal results retain earlier warnings.
The existing libconfig grammar, conversions, range checks, deprecated-option
notices, and warning fallbacks remain the parsing contract.

The standalone `receiver-configuration` target links only libconfig, FFmpeg
util, and standard-library text/format utilities. Its startup/parser tests pass
31 cases without linking receiver services. Runtime initialization separately
probes the MAC address, selects the backend, configures logging/FFmpeg, seeds
randomness, generates pairing/UUID identity, and establishes the shared level.
`LegacyConfigLease` explicitly maps all 75 settings fields plus diagnostic/path
and PulseAudio metadata into the transitional global view without copying its
mutex. It keeps settings and the diagnostic-dump tree alive through cleanup;
cleanup is registered after lease construction and runs before its destruction.
Settings-owned strings and the libconfig tree are no longer manually freed.
A receiver-linked test holds the runtime mutex across mapping, checks all five
groups and mapped pointers/channel arrays, then dumps the retained tree after
source removal. Explicit/default unreadable-file rejection also runs under root
by dropping credentials in an isolated child. Runtime consumers still use the
compatibility view, and broader migration/device acceptance work remains open.
The full pinned Release build passes 430/430 cases. The standalone startup/parser
suite passes 31/31; 35 focused startup/parser/lease/daemon cases pass under Debug
ASan+UBSan. Both loader and lease implementation objects were checked for actual
sanitizer instrumentation. Existing effective runtime log formatting and FFmpeg
error-only logging remain unchanged; accepting the configured log-format flags
would be a separate behavior change.

The session-registry slice separates lifecycle policy from runtime session data.
`SessionRegistry` owns `ManagedSession` workers through stable identity, live
category, start, stop-request, and join operations. The standalone library links
only Threads; a fresh `build/session-components` target compiles and links its
seven lifecycle cases without receiver, audio, pairing, or native service
dependencies. Admission failure and closure release ownership, finished IDs are
idempotent, category selection uses current state, and every selected worker
receives its stop request before any join. Worker resources remain owned until
joining completes.

`RuntimeSessionWorker` retains named pthread creation, cancellation, and fatal
join-error handling. `SessionState` still stops playback before closing its
socket, and the registry preserves caller cancellation masking, including
deferred pending delivery after its noexcept destructor. Existing immediate
completion, cancellation, principal replacement, and socket-lifetime integration
assertions are retained. Focused Release, ASan+UBSan, and TSan pass 21/21 cases;
the full pinned Release suite passes 437/437. Registry and runtime adapter objects
have actual sanitizer instrumentation. This boundary preserves pthread cancellation;
cooperative worker stopping and broader session encapsulation remain open.

The principal-selection slice moves admission, replacement, retirement, identity,
and generation-ticket decisions into the named module `receiver.session.principal`.
`PrincipalSelection` borrows participants through stable identity, eligibility,
and retirement operations; its library depends only on the standard library and
Threads. Its six standalone cases link without receiver, playback, pairing, or
native service libraries. Repeated selection preserves a ticket, denied
replacement preserves the current participant, and replacing a distinct object
with the same numeric ID invalidates the earlier generation. Release clears
selection without retirement; explicit clear retires it. Effects execute under
the selection lock only while their ticket remains valid.

`RuntimePrincipalSession` admits only `SessionState` participants and delegates
selection to that module. Playback/group snapshots and session mutation use the
same lock as selection; snapshot values remain owned copies. Two runtime cases
cover active playback metadata, copied group IDs, empty defaults, synchronized
group updates, and mutations of a nonselected session. Existing volume,
replacement, registry, shutdown, and playback assertions remain intact. The full
pinned Release suite passes 445/445; focused ASan+UBSan passes 23/23 and TSan
passes 36/36, including existing cancellation scenarios. Core objects contain
actual ASan, UBSan, and TSan instrumentation. The playback fixture uses the
existing `CancellationWait` helper: cancellation through `pause` initially
produced a TSan cleanup-lock visibility warning, whereas the helper's
`pthread_cond_wait` cancellation restores LLVM 23.1.3 interceptor state.
Independent review and hosted CI remain pending for this slice. Participants
must outlive selection and callbacks must not reenter or retain borrowed
pointers. Broader session encapsulation and cooperative stopping remain open.

The RTSP request-reader slice moves framing into the named module
`receiver.protocol.rtsp.request`. `RtspRequestReader` owns the pending message
and header/body storage, with explicit input, monotonic-clock, and diagnostic
ports. Its library depends only on the standard library and
`receiver-rtsp-message`; its twenty-seven standalone cases link without the receiver,
audio, pairing, or native service libraries. The runtime adapter retains socket
and cipher reads, captured transport errors, stop state, diagnostic wording,
and header-EOF descriptor closure. The framing loop and old line scanner have
been removed from `rtsp.cpp`.

Pending header/body storage uses uninitialized owned arrays. Growth copies only
already received bytes, avoiding eager initialization of the declared content
length. A bounded 5000-byte allocation-poisoning check failed with zero-filled
vector growth and passes with uninitialized storage; it also checks that the
buffered body prefix survives growth.

Seven socket cases first passed against the original reader, including actual
HomeKit cipher encryption/decryption, binary bodies, mixed line delimiters,
NUL-truncated header interpretation, negative content lengths, full headers,
and header/body EOF closure differences. Additional coverage preserves stale
ETIMEDOUT precedence at EOF. Standalone cases cover fragmented reads, exact
missing-body read sizes, phase-specific errors and timeouts, terminal CR,
split CRLF, large bodies, allocation failure, stop checks, and a warning only
after fifteen seconds, once, before the next read or stop check. Existing
partially read request cancellation still releases the request and leaves its
output null under ASan+UBSan and TSan.

After allocation review corrections, the pinned Release suite passes 480/480;
focused ASan+UBSan and TSan each pass 36/36. Core objects contain actual
sanitizer instrumentation.
Independent review and hosted CI remain pending for this slice. Only
`std::bad_alloc` is caught; pthread forced unwind propagates through RAII.
Allocation recovery is an intentional behavior fix: the original message
constructor terminated the process on allocation failure, and parser/body-copy
allocation failures escaped. These failures now return allocation-failure
status with no message and release pending resources, consistently with header
and body storage failure. Six bounded linker fault-injection cases exercise
actual message, header-array, request-line, header-list, body-growth, and
body-copy allocations across both diagnostic phases, then verify successful
retry. No host heap exhaustion is used. The PR must retain `fix` classification,
requiring a patch release through the project's release automation.
The bounded scanner avoids the original terminal-CR
uninitialized lookahead while preserving observable delimiter behavior,
including treating a following fragment's leading LF as a separate empty
line. Buffered excess, including a following request, remains body content;
pipelining correction, broader protocol decomposition, and device acceptance
remain open. Allocation-address debug tracing is no longer emitted.

The buffered-flush slice gives `BufferedFlushPolicy` ownership of immediate
and deferred request state, admission, activation, endpoint completion,
overrun handling, and reset scopes. Its ordinary header supports the many
`SessionState` consumers; its standalone library declares only standard-library,
Threads, and pure `receiver-mod23` dependencies. Raw session flush flags,
request arrays, and the shared flush-record type/capacity macro are removed.
`SessionState` owns the policy; RTSP plist parsing, playback pause/anchor reset,
packet discarding, and diagnostic formatting remain runtime responsibilities.

The policy has ten first-free deferred slots and returns owned events in a
fixed twenty-two-event result, without heap allocation or effect callbacks.
Immediate completion cancels all deferred requests, including active ones;
only inactive cancellations are logged. Deferred starts require exact sequence
matching; missed starts retain blocks until endpoint/overrun. Endpoints retain
their current packet. Twenty-three-bit masking and modular comparisons retain
wraparound behavior; timestamps are diagnostic data rather than discard
boundaries. Cached deferred start packets still repeat activation events.
Receiver initialization clears all requests, while playback initialization
clears only deferred requests. `everReadBlock == false` skips immediate
evaluation while still evaluating deferred requests.

Three real FLUSHBUFFERED dispatch characterizations passed before extraction:
immediate requests pause playback and clear its anchor, deferred requests
preserve both, and a full queue acknowledges an eleventh request with 200.
Six adapter cases now connect real plist dispatch to scripted packet decisions;
thirteen standalone policy cases cover ranges, overlap, event ordering, resets,
wraparound, first-free reuse, bounded capacity, and concurrent producers. The
standalone link contains only policy/mod23/GTest libraries. The full pinned
Release suite passes 499/499; focused ASan+UBSan and TSan each pass 61/61,
including existing RTSP dispatch and playback/player cases. Actual policy
sanitizer instrumentation is present.

The outer runtime `flush_mutex` remains around RTSP admission/pause/anchor
effects and processor evaluation/logging. The lock order is outer mutex then
policy mutex; events are logged after releasing the policy mutex. The policy
does not acquire the outer mutex. Independent review and hosted CI remain
pending for this slice. Complete buffered TCP/cipher/decoder/player acceptance,
device pause/resume, and broader audio/session ownership remain open.

The buffered-block slice moves wire metadata parsing, ChaCha20-Poly1305-IETF
authentication, owned payload preparation, and AAC ADTS framing into the named
module `receiver.protocol.ap2.buffered_block`. Its explicit format descriptor
contains only codec/channel configuration; its library links standard-library
facilities and libsodium. Runtime SSRC lookup, late-block admission, clock/flush
decisions, format preparation, mute/discontinuity policy, and player submission
remain at their existing points. The buffered processor's inline header reads,
crypto call, ADTS helper, and leading-256-byte payload buffer are removed.

The block view borrows the current wire bytes and copies 23-bit sequence,
big-endian timestamp, and SSRC metadata. Preparation validates the explicit
32-byte key span, authenticates bytes 4..11 as AAD, takes ciphertext/tag from
byte 12 up to the trailing eight-byte nonce, and front-pads that nonce with
four zero bytes. Successful payloads own their bytes; AAC prepends the same
seven ADTS bytes with 44.1/48-kHz indices, channel-seven encoding, and the
existing unsupported-rate fallback. Allocation exceptions propagate; no new
allocation-recovery policy is introduced.

The separate safety fix validates the two-byte-inclusive declared length before
the native `read_sized_block` body copy: the prefix must be complete and at
least two, the body must be 36..16384 bytes, and it must fit the destination.
Malformed framing produces a diagnostic and finishes the buffered receiver.
Wrapped tests call the actual runtime read adapter: every short declared
length, oversized prefixes, incomplete prefixes, and insufficient destinations
are rejected before any body read. The unchecked adapter made these tests
fail before the fix. Core parsing also rejects all short bodies and oversized
input before metadata reads. This requires `fix` classification and an
automated patch release, expected v6.1.4; no version is incremented manually.

A deterministic non-silent golden fixture passed against the original sodium
contract and ADTS helper before extraction. Fifteen standalone cases cover
metadata, owned bytes, AAC headers, separate AAD/ciphertext/tag/nonce tampering,
key errors, empty plaintext, and wire bounds. Actual ALAC/AAC encoders feed
encrypted blocks through preparation and synchronous `player_put_packet`;
352/1024 decoded frames remain queued after source/prepared storage changes.
The standalone link contains only the block module, libsodium, and GTest.
The full pinned Release suite passes 520/520; focused ASan+UBSan and TSan each
pass 46/46, including prior flush and player boundary cases. Actual core
sanitizer instrumentation is present. Independent review and hosted CI remain
pending. Complete buffered TCP scheduling/cancellation, device playback,
performance measurements, and broader decoder/session ownership remain open.

The buffered TCP transport slice applies Feature Envy (#77): `BoundedByteQueue`
owns bounded FIFO storage, occupancy, blocking, and terminal decisions;
`BufferedTcpTransport` owns its accepted socket, eventfd, and cooperative worker.
Their ordinary header interfaces and separate libraries keep the queue dependent
only on standard-library facilities and Threads, and the Linux socket adapter
independent of receiver configuration, logging, timing, and media libraries.
The processor uses an explicit exact-read port. Public raw ring fields, native
condition variables, `buffered_tcp_desc`, `read_sized_block`, and the legacy reader
implementation are removed.

EOF and native read errors now remain permanent after buffered bytes drain;
partial exact reads report their byte count and terminal status. Explicit stop
discards remaining bytes and wakes empty readers and full producers. Reads larger
than capacity make progress by draining fragments. Transport startup checks the
listener, eventfd, and worker creation, reports errors without an invalid join,
and permits only one start attempt. Its borrowed listener must already be
nonblocking and have a single acceptor; RTSP owns that flag setup. The worker
receives at most 4096 bytes and preserves the interruptible ten-millisecond yield
above 16384 queued bytes. The runtime retains its configured eight-MiB capacity.

The processor's transport scope ends before its outer listener cleanup, including
pthread cancellation. Transport destruction stops and joins with deferred caller
cancellation masked; the socket worker itself is never canceled. The actual
processor lifecycle test wraps only timing admission and initial player reset,
then verifies canceled completion, listener closure, and descriptor reclamation.
It does not prove every processor timing/playback cancellation point. Actual
loopback framing covers a fragmented 6000-byte body through a 31-byte queue;
malformed-prefix tests now use the real queue and prove body bytes stay unread.
Other cases cover wraparound, backpressure, partial/sticky EOF and errors,
bounded idle/full-queue stops, invalid listeners, injected thread/eventfd creation
failures, and a real peer reset retaining `ECONNRESET`.

The original 520/520 Release baseline and legacy FIFO characterization passed.
The legacy EOF regression failed in a bounded child, which was killed and reaped
after 150 milliseconds; the new queue returns terminal reads immediately.
The final Release suite passes 537/537 in 17.86 seconds. All 56 focused transport,
block/authentication/player-handoff, and session shutdown/registry cases pass
under each of ASan+UBSan and TSan. Both production transport objects contain the
respective sanitizer instrumentation. Standalone link commands contain only the
queue/socket components and GoogleTest. Local evidence is preserved under
`/tmp/buffered-transport-*`; independent review and hosted CI remain pending.
Permanent terminal reads and checked transport startup are intentional fixes,
requiring `fix` classification and the automated patch release expected v6.1.5;
no version is incremented manually. Cooperative shutdown of the whole processor,
other session workers, device playback, and controlled performance measurements
remain open. The installed receiver was neither installed nor restarted.

The buffered playback slice applies Long Method (#82) and Feature Envy (#77)
by moving play transitions, admission, warning latching, authenticated timestamp
planning, mute/skip decisions, and player accounting into the standard-library
named module `receiver.protocol.ap2.buffered_playback`. A local policy captures
the immutable desired decoded-buffer length. It knows no session, configuration,
clock, decoder, logger, or socket. Runtime retains current block validity,
transport reads, flush effects, format selection, key authentication, and player
submission. Player sequence is seeded at the existing first decoding setup;
accounting commits only after the player returns, including a zero-frame return.
The separate decoded-player `PlaybackTiming` owner is unchanged.

Compatibility includes strict lead thresholds at desired length plus 0.1/0.2
seconds, disabled-play early warnings, one warning until admission rearms it,
integer two-packet waits, and a 20-millisecond invalid-clock wait without a now
clock call. Late blocks consume without authentication. Stop resets only the
submitted packet count; sequence, expected timestamp, warning state, and first
timestamp remain until their existing update points. First AAC packets and AAC
discontinuities mute; negative gaps skip only beyond one block. Timestamp and
player sequence wrap remain defined. The historical previous-scheduled-time
value remains constant zero, so the policy intentionally preserves that quirk.
Packet waits require a positive sample rate; absent timing accepts no packet
shape, and supported runtime formats supply the rate before packet admission.

An independent safety change widens a negative timestamp gap before negation.
The signed minimum formerly overflowed both the extracted policy and the actual
processor on an authenticated encrypted block; UBSan recorded both failures.
It now deterministically skips the excessively old block without committing
accounting. This requires `fix` classification and an automated patch release,
expected v6.1.6, with no manual version increment.

The original encrypted TCP stream passed against the old decision loop before
extraction. Runtime fixtures exercise real transport and sodium authentication,
with only clocks, initial decoding setup, waits, and player effects supplied at
their boundaries. They verify clock/effect order, first mute, old-block skip,
failed authentication preserving accounting, late blocks never authenticating,
and zero-frame player returns. Standalone cases cover transition, threshold,
warning, gap, wrap, and stop/resume boundaries. Evidence logs are preserved in
`/tmp/buffered-playback-*`; hosted CI and independent review remain pending.
The Release baseline passes 537/537 in 17.62 seconds and the final suite passes
548/548 in 17.93 seconds. All 67 focused policy, adapter, block/authentication,
transport, player-handoff, and session lifecycle cases pass under each of
ASan+UBSan and TSan, including actual processor cancellation.
The module links only its standard-library component and GoogleTest in isolation;
its production objects contain ASan+UBSan and TSan instrumentation. Full processor
cooperative shutdown, device playback, and controlled timing/performance
acceptance remain open. The installed receiver was not restarted or installed.

The buffered receiver coordinator slice completes extraction of the buffered
decision loop into `receiver.protocol.ap2.buffered_coordinator`. It applies
Long Method (#82) and God Service (#78): the coordinator owns stable 16-KiB wire
storage, borrowed block views, copied metadata/history, format selection,
block accounting, cached/fresh read state, and its playback policy. Three focused
ports supply live session observations/effects, clocks, and audio effects. The
processor retains decoder/resampler reset, flush reset, transport ownership,
cancellation cleanup, concrete port adapters, and a call to `run()`; it contains
no block-reading, parsing, admission, authentication, or submission decisions.

The component owns `readBufferedAudioBlock` and links only the existing block,
playback, and format components plus their standard-library/libsodium dependencies.
Its public protocol and implementation contain no session, common runtime,
configuration, player, FFmpeg, logging, or backend types. Ports return coherent
values and accept typed diagnostics whose metadata is copied. Runtime formatting
preserves the existing diagnostics. Session flush evaluation and logging execute
inside the original cancel-safe flush mutex scope. Player payload spans are
consumed synchronously and must not be retained. Fixed wire storage is left
uninitialized until read; allocation exceptions propagate, and forced unwind is
not caught. Port/input lifetimes remain the caller's responsibility.

`advance()` performs one original iteration after its first timing-readiness wait
and initial player reset. It observes prefix/body occupancy before parsing,
counts even unknown-format blocks, preserves metadata history and initial sequence
seeding, and evaluates flush on every nonterminal iteration before admission.
Cached clock/early/disabled waits retain stable wire bytes; playback restart asks
for fresh input. Missing key, failed authentication, late/too-old consumption,
zero-frame player accounting, and the constant-zero previous schedule quirk retain
their established behavior. Terminal prefix/body, EOF, error, and malformed-size
results return before flush/admission. `run()` repeats until such a terminal result.

Thirteen standalone encrypted-pipeline cases cover startup/effect order, complete
submission, codec/metadata history, cached storage after input mutation, disabled
restart, cached flush, preparation failure, late consumption, old-block skip,
zero return, unknown formats, and terminal boundaries. The existing real encrypted
TCP loopback and actual processor cancellation fixtures still exercise production
wiring. Evidence and per-scenario red/green logs are preserved under
`/tmp/buffered-coordinator-*`; independent review and hosted CI remain pending.
The Release baseline passes 548/548 in 17.34 seconds; the final suite passes
561/561 in 17.44 seconds. All 80 focused coordinator, policy, block/authentication,
transport, player-handoff, and session lifecycle cases pass under each of
ASan+UBSan and TSan.
The isolated link contains only coordinator/block/playback/format libraries,
libsodium, and GoogleTest. Coordinator, framing, and module-interface objects
contain the respective sanitizer instrumentation. This is a structural slice:
the existing v6.1.6 version remains unchanged, with no new release bump required.
Whole-processor cooperative stop, other workers, device acceptance, and controlled
performance measurements remain open. No package installation or live restart
was performed.

The realtime audio ingress slice applies Long Method (#82) and Feature Envy (#77)
through the standard-library/libsodium module
`receiver.protocol.ap2.realtime_audio`. Its decoder owns authenticated plaintext
and copies the realtime 16-bit sequence and big-endian timestamp. The stripped
layout has a ten-byte header; bytes 2..9 are AAD, ciphertext/tag begin at byte 10,
and the trailing eight-byte nonce receives four leading zero bytes. Sizes below
34 or above 4094 and missing/wrong-length keys are explicit errors. Sequence
bytes remain outside AAD. This layout is deliberately independent of buffered
23-bit sequence parsing, while using the same deterministic wire fixture.

`RealUDPIngress` owns its 4096-byte receive storage and coherent datagram/RNG
ports. It preserves the strict full-datagram `>36` gate: an authenticated empty
34-byte stripped payload remains valid for control, while its 36-byte UDP form
is rejected before RNG or authentication. Fraction zero bypasses RNG; otherwise
only a draw strictly greater than the captured drop fraction admits a packet.
Drop-fraction writes are confined to startup configuration/lease setup. The
runtime UDP loop supplies sockets, randomness, cancellation cleanup, diagnostics,
and the fixed ALAC-44100 stereo submission adapter. D6 retains its original
control framing/drop guards, strips six bytes, and uses the same decoder and
submission adapter. The core knows no session, configuration, socket, pthread,
player, or FFmpeg types; allocation exceptions propagate without catching forced
unwind. Dead UDP sequence-warning tracking and inline crypto/parsing are removed.

The original helper's successful golden/non-silent plaintext and failed-MAC empty
submission were characterized before extraction. Three no-failed-submission
regressions then failed against the actual helper, UDP receiver, and D6 receiver.
The separate fix rejects failed authentication before calling the player, avoiding
packet-window/accounting effects from unauthenticated input. Authenticated empty
control payloads still call the player. This requires `fix` classification and
the automated patch release expected v6.1.7; no version is incremented manually.

Fourteen standalone cases cover metadata/owned plaintext, every short length,
exact maximum/empty payload, key errors, separate AAD/cipher/tag/nonce mutations,
sequence mutation/wrap, receive errors, exact 36/37-byte boundaries, and strict
random dropping. Runtime fixtures verify exact submitted plaintext/metadata,
fixed codec/mute/gap values, failed-MAC rejection, and native pthread cancellation
closing actual loopback UDP/control sockets. They supply player effects and do
not add a new end-to-end realtime decoder/device acceptance claim. Existing
non-silent ALAC/AAC handoff and session lifecycle cases remain in focused checks.
The isolated link contains only realtime audio, libsodium, and GoogleTest; both
production module objects contain ASan+UBSan/TSan instrumentation. Local evidence
is preserved under `/tmp/realtime-ingress-*`; review and hosted CI remain pending.
The Release baseline passes 561/561 in 17.37 seconds; the final suite passes
580/580 in 17.71 seconds. All 55 focused realtime, block/authentication,
player-handoff, and session lifecycle cases pass under each of ASan+UBSan and TSan.
AP2 control timing/anchor policy, whole-worker cooperative stopping, device
acceptance, and controlled performance measurements remain open. No package
installation or live restart was performed.

The control packet slice applies Long Method (#82) and God Service (#78) through
the standard-library module `receiver.protocol.ap2.control_packet`.
`ControlPacketCoordinator` owns initialization, packet counting, stale/drop gates,
routes, and ordered effects; `ControlLatency` owns checked latency arithmetic.
The complete AP2 control decision loop is removed from `rtp.cpp`. Runtime retains
`recvfrom`, peer address storage, effect formatting/application, shared configuration
writes, anchor publication, D6 authentication/submission, and cancellation cleanup.
The control-only session initialization field is removed. A focused standard
function supplies random draws without linking control to realtime crypto.

The fixed start-time gate preserves signed elapsed time, strict two-millisecond
non-sentinel rejection, and sentinel bypass. Initialization occurs even on the
first read error; sub-28-byte datagrams do not draw. First-packet counting precedes
random dropping, fraction zero bypasses RNG, and equality drops. Realtime
EAGAIN/EWOULDBLOCK resets the anchor/count; buffered timeout remains silent.
Admitted effects request peer capture only if runtime storage is absent. D7
clock/time/frame values are copied in big-endian order, and anchor subtraction
wraps as before. D6 and unknown actions borrow the current packet only until
the next read; malformed diagnostics own their byte count. Eight bounded slots
hold ordered effects without a heap-backed event list.

Backend buffer and the session warning flag remain live per-packet inputs.
An already-issued warning retains a negative calculated latency. The following
timing-ownership slice replaces the original compatibility offset write; runtime
socket lifetime remains a migration gap. RTSP starts the control
worker before optional input sample-rate setup, so finite zero-rate early anchors
remain valid: normal stream latency stays 88200, with no warning/offset change.
Zero-rate nonpositive warning diagnostics represent infinity/NaN explicitly while
retaining warn-once, offset, latency, and anchor effects.

The separate safety fix checks finite truncated frame conversions and widens
latency arithmetic before checking int32 result ranges. The original calculator
and real UDP handler both produced recorded UBSan failures for INT32_MAX plus
11025. Such invalid calculations now produce a typed diagnostic without latency
or anchor publication. Normal/truncated fractional boundaries and signed frame
wrap remain preserved. This requires `fix` classification and the automated patch
release expected v6.1.8; no version is incremented manually.

The original sentinel-D7 UDP characterization verified actual anchor ownership,
big-endian fields, latency, shared-offset reset and warning state before extraction.
Thirteen standalone cases cover latency bounds, initialization, stale/drop gates,
timeouts, routes, live settings, borrowed views, and owned anchor metadata. Four
real control cases cover normal publication, invalid timing rejection, positive
early zero-rate anchors, and zero-rate negative warn-once effects. Existing realtime
D6 authentication and native cancellation fixtures remain in focused checks.
The Release baseline passes 580/580 in 17.54 seconds; the final suite passes
597/597 in 17.71 seconds. ASan+UBSan and TSan each pass 54/54 focused cases.
The independent control link contains only its component and GoogleTest, and
both production module objects contain sanitizer instrumentation. Evidence is
preserved under `/tmp/control-coordinator-*`; review and hosted CI remain pending.
Shared configuration isolation, full cooperative worker shutdown, device acceptance,
and controlled performance measurements remain open. No package installation or
live restart was performed.

The shared RTP timing slice applies Feature Envy (#77):
`SharedRtpTimingSettings` owns the process-wide live offset and its one-way
disable operation. Its ordinary header and independently linked standard-library
target use `atomic<double>`; no global settings accessor is introduced.
The immutable `ReceiverSettings` initial offset remains parsed and logged, but
the mutable compatibility field and lease copy are removed. Remaining
`config.audio_backend_latency_offset` references belong only to the loader's
private `ParsedConfig`, not the runtime global.

Application startup creates an explicit shared owner before starting the listener.
The listener captures ownership and passes the same owner to accepted sessions.
`SessionState::create` value-initializes the existing defaulted constructor before
binding its private shared pointer, preserving raw null/zero fields and fd=-1.
Default isolated sessions each own fresh zero settings. Existing and later
sessions observe a control warning's disable, while independent owners are
unchanged; a session can retain its owner after the listener stops.

A native control-worker/concurrent second-session clock-conversion test produced
an actual TSan race between the original `rtp.cpp` control write and anchor read.
The atomic owner now makes that same production-path regression pass. Forward
and reverse production clock conversions observe initial offset and disabled
offset correctly. This is a race fix requiring `fix` classification and the
automated patch release expected v6.1.9; VERSION is not changed manually.

The Release baseline passes 597/597 in 17.66 seconds; the final suite passes
606/606 in 17.79 seconds. ASan+UBSan passes 62/62 focused cases in 2.50 seconds,
and TSan passes 62/62 in 3.30 seconds. The independent settings executable links
only its component and GoogleTest; its production object contains both profiles'
instrumentation. Red/green, full/focused, link, and instrumentation logs remain
under `/tmp/shared-rtp-timing-*` for review.

The first listener lifetime fixture cancelled a native conversation blocked in
socket read and produced TSan registry-cleanup reports, preserved separately in
`shared-rtp-timing-tsan-native-cancellation-initial.log`. Both reported registry
accesses use the same mutex. Existing `cancellation_wait.hpp` documents LLVM
23.1.3 cancellation interceptor state limitations; the warning is not evidence
of an offset-owner race. The ownership fixture now closes the peer and waits for
actual conversation completion before listener stop, without suppressions or
production changes. Native conversation cancellation cleanup remains a
verification limit; existing focused session cancellation tests remain green.

The per-session warning flag and other compatibility configuration remain outside
this slice. Full cooperative worker shutdown, device acceptance, controlled
performance measurement, and whole-goal completion remain open. No installation
or live restart was performed.

The resend ownership slice applies Feature Envy (#77) and Long Method (#82).
`ResendCoordinator` owns the serialized request/backoff transaction in a
standard-library component; its IO protocol supplies clock, random draw, and
one eight-byte send result. `RemoteControlPeer` owns native address capture and
owned snapshots behind its own mutex in a separate component. Both objects are
embedded in `SessionState`; the raw peer address/length and failure timestamp
are removed. No global owner or settings accessor is introduced.

The runtime rejects inactive requests before reading the peer, takes one owned
snapshot, and uses it throughout timeout setup and send. The original 100ms
SO_SNDTIMEO failure diagnostic still occurs before send, and setup failure does
not prevent sending. The coordinator preserves the D5 wire bytes, first sequence
in big endian, count truncation to uint16, strict greater-than-300ms unsigned
backoff, zero-timestamp sentinel, random equality dropping, and any non-minus-one
send result clearing backoff, including zero and partial positive results.
Failure errno is captured at send. Only `rtp_initialise` resets backoff in runtime.
Synchronous IO must not reenter the coordinator; cancellation unwinds its RAII
transaction lock without catching forced unwind.

Peer capture accepts bounded IPv4/IPv6 native addresses and retains the first
endpoint until clear. Snapshots preserve length and exact copied bytes, with
unused storage initialized. Control cleanup closes the socket before clearing
the peer. Native D7 and D6 traffic verifies first-peer retention, exact resend
delivery, cleanup, and subsequent capture of a second endpoint. Standalone tests
cover invalid native lengths/families, owned snapshot retention, concurrent
capture/clear integrity, all backoff boundaries, serialized failures, and
cancellation releasing the transaction lock. Runtime IO tests also verify early
no-IO outcomes, timeout failure still sending, and initialization reset mapping.

Before extraction, the actual native control handler and `rtp_request_resend`
produced a recorded TSan peer-length capture/read race. The same native traffic
regression now passes with the mutex-protected owner. This requires `fix`
classification and the automated patch release expected v6.1.10; VERSION remains
6.1.9 pending release automation. The Release baseline passes 606/606 in 17.85
seconds; the final suite passes 619/619 in 17.99 seconds. ASan+UBSan passes 69/69
focused cases in 2.09 seconds and TSan passes 69/69 in 2.83 seconds. Independent
coordinator/peer executables link only their respective component and GoogleTest;
both production objects contain both sanitizer profiles' instrumentation.
Red/green, native race, full/focused, link, and instrumentation evidence remains
under `/tmp/resend-ownership-*` for review.

The initial post-extraction native socket-read cancellation fixture reported
cleanup access warnings, preserved in `resend-ownership-native-cancellation-initial.log`.
Peer snapshot and clear use the same mutex; LLVM 23.1.3 interceptor state after
native cancellation remains a verification limitation. Borrowed control-fd
lifetime and the plain `rtp_running` flag were explicit lifecycle gaps at this
stage; the following control-channel slice removes both raw fields. The traffic
fixture handshakes at the existing `CancellationWait`
cancellation point after actual UDP capture/send, without production changes or
suppression. Existing native control/audio cancellation tests remain in the
focused suite; this does not establish general concurrent socket-close safety.
Retransmission range planning, full cooperative shutdown, device acceptance,
performance measurement, and whole-goal completion remain outside this slice.
No installation or live restart was performed.

The control datagram ownership slice applies Feature Envy (#77) and Long Method
(#82). `ControlDatagramChannel` owns the UDP descriptor, eventfd wake, admission
state, and counted operations. `SessionState` embeds the owner; the public raw
control descriptor and `rtp_running` field are removed from all source/tests.
Setup binds into a local descriptor and transfers it only after successful
adoption; adoption failures close the caller's descriptor. The defaulted session
constructor and existing player activation points remain unchanged.

Resend acquires one admitted send lease before taking the owned peer snapshot.
Receive also holds a lease, including before playback activation, preserving
early anchors. Indefinite UDP readiness polling uses the descriptor and stop
event together; nonblocking recv avoids a readiness/stop blocking race. A stop
event exits the control loop before coordinator handling and does not fabricate
an EAGAIN anchor reset or introduce a receive timeout. Player cleanup closes
admission and signals stop at entry, while retaining existing worker cancellation
and joins. Receiver cleanup closes the channel before clearing the peer.

Close masks deferred cancellation, stops admission, wakes operations, waits for
all leases, and closes both descriptors once. Native IO occurs outside the
lifecycle mutex. The external contract requires the owner to outlive leases,
holders to release before calling close, and the previous receiver to be joined
before re-adoption. Arbitrarily retained tokens can delay close; this component
does not promise to bound arbitrary user callbacks or the D6 audio sink.

An actual old runtime regression admitted resend inside the wrapped native
sendto, then observed cleanup complete and its captured descriptor already
closed. It now verifies cleanup waits and the descriptor remains valid until
the send releases its lease. Separate tests cover failed/double adoption,
idempotent close, re-adoption, preactivation native peer/bytes, concurrent receive
and send, indefinite receive wake, and actual native poll cancellation releasing
its lease. A real D7 control worker publishes its early anchor and then joins
normally after the stop event, without forced cancellation in that fixture.

A separate deliberate fix retains SO_SNDTIMEO setup and its warning before send,
but enforces a 100ms steady-clock budget even when setup fails. MSG_DONTWAIT
send retries only would-block results through POLLOUT/stop polling; other native
errors retain failure semantics. Stop returns ECANCELED; deadline expiry returns
EAGAIN. Recorded red/green tests verify timeout-setup failure still has a bounded
attempt, one blocking poll rather than a busy loop, and stop interrupts the
would-block wait. Existing wire, backoff, partial-send, D6 authentication, and
early-zero-rate D7 behavior remain covered. These lifecycle/budget fixes require
`fix` classification and the automated patch release expected v6.1.11; VERSION
remains 6.1.10 pending release automation.

The Release baseline passes 619/619 in 17.89 seconds; the final suite passes
630/630 in 18.28 seconds. ASan+UBSan passes 80/80 focused cases in 2.52 seconds,
and TSan passes 80/80 in 3.40 seconds, including native poll cancellation and
normal worker stop. Final source review also identified a transient wake-count
leak if stop was cancelled inside eventfd write. The focused red timed out during
close; a RAII wake lease now releases that operation on unwind, and the same
regression passes in all three profiles. The independent transport executable links only the channel,
native peer component, and GoogleTest; its production object contains both
sanitizer profiles' instrumentation. Evidence remains under
`/tmp/control-channel-*`; no new cancellation warnings were observed in these
checks. This establishes these lease/IO paths, not general device shutdown.
Whole-worker cooperative stopping, D6 downstream blocking, other socket owners,
device acceptance, performance measurements, and whole-goal completion remain
open. No installation or live restart was performed.

The event command exchange slice applies Long Method (#82) and Feature Envy
(#77). `EventCommandExchange` owns the accepted plaintext channel and serializes
complete command write/read pairs behind its standard mutex. Its independently
linked ordinary-header component depends only on the standard library and
Threads. It frames the exact POST /command headers and binary body, returns
legacy signed counts plus typed status, and owns up to 4096 reply bytes.
Typed diagnostics execute at the original points inside the transaction;
observers must not reenter or retain their borrowed reply view.

Missing channels return -1 without IO. Short/error writes return -1 without a
read; complete writes perform one read, preserving EOF zero, error minus one,
binary NUL bytes, and single-read native cipher behavior. Native encrypted reads
may return split chunks; the component does not aggregate or parse RTSP replies.
The full 4096-byte reply is verified through the initialized plaintext port.
The original packet[count] terminator could index beyond its 4096-byte buffer;
the separate fix uses bounded logging and the exchange uses owned byte views,
with no terminator write. Text diagnostics still stop at the first NUL.

Runtime retains libplist serialization, informational dictionaries, existing
HomeKit cipher functions, and diagnostic formatting. `NativeEventChannel` owns
only its accepted descriptor and borrows the session's event cipher bundle.
Its local descriptor guard retains ownership if allocation fails, rejected
attachment destroys the candidate, and deferred cancellation is masked during
descriptor destruction. Destruction never accesses the borrowed cipher. The
receiver accepts locally, attaches the first channel, invokes updateInfo, and
closes the exchange before existing group/remote cleanup. RTSP joins the event
receiver and closes the exchange before releasing the event cipher bundle.
SessionState's raw event descriptor/mutex and RTSP init/destroy calls are removed.

The actual receiver child fixture failed against the legacy positive-only accept
condition and now verifies valid descriptor zero reaches attach/updateInfo and
is closed by cleanup. Normal real channel-4/channel-1 HomeKit socketpair tests
verify two consecutive commands with a frozen binary-plist body and exact wire
headers, preserving cipher counter state and returned counts. Core cases cover
framing, failure rows, initialized binary/full replies, pair serialization,
first-owner retention, close ownership, and transaction cancellation unwind.

An actual native response-read cancellation fixture passes in Release and
ASan+UBSan: after the sender joins, an independent thread acquires the exchange
through close and closes the accepted fd before cipher release. Initial native
TSan instead reported a double-lock at close, even though the functional close
completed. That report is preserved in
`/tmp/event-command-native-cancellation-tsan-initial.log`. Only TSan uses the
existing CancellationWait seam at that read cancellation point; production is
unchanged and no warning is suppressed. Its passing result is seam-based,
not clean native-read cancellation TSan evidence. The native TSan unwind
observation remains a tooling/verification limitation; Release/ASan independent
acquisition establishes that the actual mutex is released on these paths.

The Release baseline passes 630/630 in 18.04 seconds; the final suite passes
644/644 in 18.29 seconds. ASan+UBSan passes 64/64 focused cases in 2.92 seconds
and TSan passes 64/64 in 3.70 seconds with the stated cancellation seam. The
independent executable links only the exchange component and GoogleTest; the
production object contains both sanitizer profiles' instrumentation. Logs under
`/tmp/event-command-*` retain boundary/descriptor-zero evidence, native golden,
independent close, full/focused checks, link, and instrumentation. The fixes
require `fix` classification and automated patch release expected v6.1.12;
VERSION remains 6.1.11 pending release automation.

The review follow-up enables the actual runtime reply diagnostic at debug level
3 through `ap2_event_port_post_command`, using an initialized 4096-byte plaintext
reply. It verifies the returned count and exactly bounded output, then verifies
that an embedded NUL still clips diagnostic text without changing the count.
Temporarily restoring unbounded `%s` logging produced an ASan read of 4097 bytes
past the reply allocation; the original production source was restored
byte-for-byte afterward. Evidence is retained in
`/tmp/event-command-review-asan-red-report.2366636` and the review green logs.
All 15 event-focused cases pass in Release, ASan+UBSan, and TSan with the existing
cancellation limitation stated above. Full Release now passes 645/645 in 18.33
seconds. This follow-up changes only the test and this evidence record.

Close may wait on blocking IO as the old mutex did; cancellation and joins are
still required. Event listener/thread ownership, complete cooperative shutdown,
runtime info/global settings, device acceptance, performance measurement, and
whole-goal completion remain open. No installation or live restart was performed.

Each delivered PR received independent review and passed all four AMD64 CI
configurations before merge. The Stage 1 work includes a non-silent stereo ALAC
fixture comparing every decoded sample; a temporary left/right decoder-plane
swap made that fixture fail while the original silent fixture still passed.
The injected fault was removed before publication.

The original post-Stage-2 Release baseline at `558b9907` passed **310/310 tests**
in **14.20 seconds**. Independent review of typed volume passed **324/324**
in **14.38 seconds**. The combined typed-volume and TLV-fix Release build at
`f40cc70b` passed **331/331** in **13.97 seconds**; independent TLV review also
passed all seven focused cases. Recorded pre-delivery sanitizer checks passed
71 typed-volume and seven TLV cases; hosted CI subsequently passed all four
AMD64 configurations for each PR and for the release.

The build uses the pinned Clang 23.1.3 C++26 toolchain. Reproduction after
configuring the same Release toolchain:

```sh
cmake --build build/resume-volume-review --parallel 2
ctest --test-dir build/resume-volume-review --output-on-failure
rg -n '\bassert\(' tests --glob '*.cpp'
```

The final search returns no matches (status 1). The test duration measures the
suite, including configuration checks, rather than playback performance.

The updated baseline includes bundled NQPTP from PR45 and README changes from
PR46. It passed **354/354** Release cases. The monitor slice passes **369/369**
Release cases, plus **21/21** targeted cases under each of Debug ASan+UBSan and
TSan. TSan also passed 30 repeated cancellation/concurrency checks. The actual
monitor object was checked for sanitizer instrumentation. A real `EAGAIN`
regression runs in an isolated child, including under root, and verifies safe
stop, retry, and destruction after failed worker creation. The parent retains
its credentials and resource limits. Hosted CI remains the merge gate.

## Release and installed validation

Release automation produced **v6.0.0**, the expected major change for removing
legacy CLI flags. The tag's `VERSION`, changelog breaking-change entry, Debian
package version/architecture, and downloaded `SHA256SUMS` were checked. The
release contains the AMD64 package and checksum file, with no ARM package.
The extracted binary reports version 6.0.0 and `sysconfdir:/etc`; validating
the packaged `shairport-sync.conf.sample` succeeds without services.

PR43 triggered the expected patch release **v6.0.1**. Its successful release
workflow verified all four AMD64 configurations before publication. The tag,
`VERSION`, changelog, downloaded checksum, Debian version/architecture,
extracted binary version, and packaged sample configuration were checked.
Assets contain only `shairport-sync_6.0.1_amd64.deb` and `SHA256SUMS`, with no
ARM package. At that checkpoint, v6.0.1 was not installed and the receiver
remained v6.0.0.

The v6.0.0 package was installed in the user's existing setup. Its existing receiver
configuration passed validation and was preserved. A systemd user-service
override selects `/usr/bin/shairport-sync --config` with that configuration.
The service remained active with no restart or startup error, connected to
the existing PulseAudio-on-PipeWire environment, and advertised through
Avahi. NQPTP was already running. The user subsequently reported that the
installed v6 works correctly and confirmed stereo playback.

This user report validates stereo operation in that setup; the sender device
and app were not specified. It does not establish the surround, pairing,
Home integration, reconnect, or multiroom test matrix.

Since that historical installation check, PR45 shipped the bundled timing
companion in **v6.1.0**. At the start of the monitor slice, the installed package
was verified as 6.1.0 and its receiver service was active. This migration did
not install or restart it; no additional device-playback claim follows from
the package/service check. Keep the installed v6.1.0 unchanged unless another
installation is requested.

## Work still open

Stage 4 remains open for the RTSP conversation, playback, and session workers:
request stop, wake blocked workers, join, then release their resources. The
activity-monitor slice preserves caller-configured blocking hooks and
nonblocking timeout hooks. A blocking caller hook can delay stop; effect
admission is serialized without a FIFO guarantee. Broader subprocess ownership
and realtime system-clock adjustments remain outside its verified scope.

`ConfigurationLoader` now returns owned settings with explicit environment
inputs. Runtime consumers still use a global compatibility view held by the
process-lifetime settings lease; removing that view remains migration work. `popt` is
still used for subprocess-hook argument splitting. Stage 4 cooperative worker
stopping, Stage 5 session encapsulation, Stage 6 protocol decomposition,
Stage 7 audio/timing ownership, Stage 8 infrastructure, Stage 9 bundled pairing,
and Stage 10 transitional C removal are not complete. First-party C pairing
and C test/fixture compilation remain; the project is not yet CXX-only.

Detailed device acceptance for realtime/buffered playback, surround,
pause/resume, reconnect, Home, pairing/re-pairing, and multiroom timing remains
pending, as do controlled before/after CPU, memory, latency, allocation, and
underrun measurements. The single successful installed setup does not close
those gaps. God Service (#78) remains the design criterion: decisions belong
to the objects that own their information.

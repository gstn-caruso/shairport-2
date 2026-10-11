# Verification evidence

## Current delivery status

The companion was delivered in PR45 and release v6.1.0. It remains bundled in
the verified v6.1.16 receiver release; all four post-merge CI profiles and package
checks passed. See [current validation](../../VALIDATION.md) and
[architecture](../../docs/architecture.md) for the repository-wide state.

The implementation log below is historical: its branch/worktree, old counts,
local paths and pending-publication statement describe the original checkpoints.
They do not mean delivery or CI is still pending. Upstream identity/provenance
and the isolated-versus-device validation limits remain unchanged.

## Historical implementation log

Branch: `feat/bundled-nqptp`, based on `origin/master` at `6c26b8b7`.
Fresh Release baseline: pinned Clang 23.1.3 / C++26, 331/331 tests passed
in 14.56 seconds. The user's explicitly requested worktree is isolated from
untracked files in the original checkout.

## Build and identity

Expectation: committed companion source builds under the receiver's toolchain,
and help/version complete without starting services.
Red: CTest `companion-version` and `companion-help` failed because the companion
executable did not exist (`build/identity-red.log`).
Green: CMake built `shairport-sync-nqptp`; both tests passed. Version identifies
the shared release version, NQPTP 1.2.8 and smi10. Companion compilation uses
`-std=c++26 -pedantic-errors`, including its entry point.

Timing and lifecycle behavior, packaging and sanitizer checks are recorded in
the following scenarios. No isolated check establishes real AirPlay playback,
multiroom synchronization or privileged systemd service operation.

## Timing characterization

The six `NqptpTiming.*` cases first passed against the untouched pinned upstream
C implementation, then passed against the bundled C++ implementation. This is a
behavior-preserving characterization scenario, so the upstream baseline is the
oracle rather than an invented failing business expectation.
The same test source supports `-DNQPTP_REFERENCE`: compile the upstream
`nqptp-clock-sources.c`, `nqptp-message-handlers.c`, `general-utilities.c`,
`nqptp-utilities.c` and `debug.c` with `cc -DCONFIG_FOR_LINUX -I<upstream>`;
link those objects and `tests/nqptp_timing_test.cpp` with Clang C++26,
`-DNQPTP_REFERENCE -I<upstream> -lgtest_main -lgtest -pthread -lrt`.
The reference adapter suppresses only network awakening announcements.

Observed offsets for the deterministic smoothing sequence: `9000000000`,
`9001000000`, `9001000000`, `9002187500`, `9002177735` nanoseconds.
Changing the announced grandmaster restarts smoothing at `10000000000`.
First-peer selection, peer replacement, signed Follow_Up correction,
unchanged Sync semantics, pause, end, brief resume, expired resume and clearing
the timing group have matching shared-memory results. No runtime network or
privileged operation was used for these cases.

## Datagram boundaries

Red: `MalformedControlPreservesAnEstablishedTimingGroup` and
`TruncatedPacketsDoNotMutateClockState` failed against the imported implementation
(`build/boundary-red.log`). Unknown commands erased peer state; short Follow_Up
packets changed counters; negative Announce length bypassed an unsigned guard.
Green: all nine timing cases pass after validating control commands before
mutation and checking signed lengths before reading packets. Minimal Follow_Up
packets no longer trigger unconditional reads of optional TLV diagnostics.
Timing calculations remain covered by the six upstream oracle cases.

## Runtime ownership and restart

Red: ownership tests failed to link against the imported implementation because
it provided only process-global `exit`/`atexit` cleanup, not the declared Runtime
ownership API (`build/lifecycle-red.log`). Green: eight Runtime cases verify
startup rollback on required-port conflicts (including either IPv4 or IPv6
while the other family could bind), preservation of existing shared-memory
contents, rollback on injected sizing/mapping failure, normal restart and
SIGTERM shutdown followed by restart. These tests bind distinct loopback
endpoints on unprivileged ports with per-process shared-memory names.
An unavailable IPv6 kernel is an explicitly reported skip for that one case.
No production sockets, `/nqptp` object or installed service were touched.

The IPv6 conflict case initially failed: this host's `/etc/hosts` resolves
`localhost` only to IPv4. Runtime now binds both numerical loopback addresses
explicitly, skipping only an unavailable IPv6 protocol. The repeated companion
selection passed 19/19 cases, including both family conflicts.

## Receiver integration

Baseline: selecting `^NqptpIntegration\.` with `--no-tests=error` failed because
there was no successful companion/receiver integration check. Green: three
integration cases passed in 0.67 seconds using actual companion UDP sockets,
the real shared-memory object, and the receiver's existing
`ptp_shm_interface_open` / `ptp_get_clock_info` reader.
Announce/Sync/Follow_Up and T/B/E/P produce the expected receiver statuses;
the first sample's `sample_time + offset` equals its injected master timestamp.
Wrong source port, wrong PTP version, inconsistent declared length and an
oversized truncated datagram produce no published clock.
After companion restart the old receiver mapping retains its old sample;
closing and reopening the receiver mapping observes the new object and resumes
timing. All endpoints use distinct loopback addresses, unprivileged ports and
unique object names. These checks do not establish AirPlay playback or multiroom
synchronization.

## Installation and Debian package

Red: the staged installation failed `tests/nqptp_package_test.cmake` because the
system companion unit was absent (`build/package-red.log`). Green: the staged
installation and the extracted `shairport-sync_6.0.1_amd64.deb` both pass that
contract: binaries have matching versions; system/user units, provenance,
COPYING and GPLv2 LICENSE are present; no activation links or Debian maintainer
scripts exist. Dependencies explicitly include `avahi-daemon`, `systemd` and
`pulseaudio | pipewire-pulse`, in addition to generated library dependencies.
`tests/nqptp_unit_test.cmake` runs `systemd-analyze verify` against a staged
executable path and passed without starting a service. Configuring the prefix
`/opt/shairport-prefix-check` generated matching paths for both units.

Reproduce with `DESTDIR=<stage> cmake --install <build>`, then
`cmake -DROOT=<stage> -DPREFIX=/usr -DVERSION=<version>
-P tests/nqptp_package_test.cmake`. After `cpack`, extract the package with
`dpkg-deb -x <deb> <root>` and repeat with `-DPACKAGE=<deb>` and the extracted root.
Unit validation uses the same ROOT/PREFIX with `tests/nqptp_unit_test.cmake`.
All successful installation checks used staging directories; no host binary or
unit was installed, enabled or started.

## Numeric peer syntax compatibility

Final boundary inspection found that `inet_pton` rejects valid numeric host
forms accepted by upstream's `AI_NUMERICHOST` parser. A seventh upstream oracle
case passed for IPv4 `127.1` and scoped IPv6 `fe80::1%lo`, then failed against
the bundled guard (`build/numeric-host-red.log`). The guard now uses the same
numeric-only address parser as upstream before mutation. All peer syntax and
timing characterization cases pass; no DNS lookup is introduced.

## Four configurations and CI

Expectation: the existing receiver suite and companion tests run with the same
compiler in Release, Debug, ASan+UBSan and TSan; companion object files carry
sanitizer instrumentation; install/package checks run without activation.
Baseline: the existing workflow had no assertions for companion object
instrumentation, service/provenance installation or Debian runtime dependencies.
The workflow now checks those contracts in addition to its existing matrix.

| Configuration | Full suite | Final companion selection after syntax/attribute changes |
| --- | --- | --- |
| Release | 354/354, 15.22 s | 23/23 |
| Debug | 353/353, 15.24 s | 23/23 |
| ASan+UBSan | 353/353, 27.76 s | 23/23 |
| TSan | 353/353, 44.00 s | 23/23 |

The extra final case characterizes upstream numeric host syntax. Focused
rechecks cover every companion test after that correction; unchanged receiver
tests were not repeated in the other three configurations. Leak detection
remained enabled, UBSan was nonrecovering, and TSan ran without infrastructure
startup failures. Three GNU unused-parameter annotations were replaced by
standard `[[maybe_unused]]` in a separate structural commit with 23/23 cases
green before and after.

`nm -u` verified `__asan_init` in the companion runtime object and UBSan handlers
in the packet-handler object; both corresponding TSan objects contain
`__tsan_init`. All four current staged installations passed the companion
package contract and `systemd-analyze verify`. The refreshed Release Debian
package passed the same checks on extracted files and control metadata.

Local logs are under `build/`: `reference-tests.log`, `release-final-tests.log`,
`debug-tests.log`, `asan-tests.log`, `tsan-tests.log`, the three
`*-final-companion-tests.log` files, and `package-final-build.log`.
Reproduce native checks with the commands in BUILD.md and select
`ctest --test-dir <build> -R '^Nqptp|^companion-' --no-tests=error --output-on-failure`
for the final companion cases. Remote CI, independent review, PR and release
publication are delivery steps still pending this local implementation report.

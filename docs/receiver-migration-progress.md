# Receiver migration status

Updated **2026-10-11** for **v6.1.16**, delivered through
[PR71](https://github.com/gstn-caruso/shairport-sync/pull/71). The current source
and component map are in [architecture](architecture.md). The migration is
**incomplete**; merged components do not establish device or performance
acceptance.

## Delivered and verified

| Area | Delivered boundary | Delivery |
| --- | --- | --- |
| Operational interface | Owned startup options/settings, foreground operation and service-independent checks | PR38, PR53 |
| Volume / audio | Typed volume, independent decoder/resampler, output setup and PCM queue | PR42, PR49–52 |
| Session policy | Registry separated from runtime workers; principal selection module | PR54–55 |
| RTSP | Independent messages, parameter handling and incremental request reader | PR51, PR56 |
| Buffered reception | Owned flush/block/playback/coordinator and bounded TCP transport | PR57–61 |
| Realtime/control/timing | Explicit ingress/control/resend ports, shared timing owner and control descriptor leases | PR62–66 |
| Event commands/lifecycle | Owned replies/transactions, listener and cooperative IO interruption | PR67–68 |
| Cipher transport | Private RAII cipher/buffers, explicit byte IO and diagnostics | PR69 |
| Pairing management | Synchronized directory module and explicit application/session ownership | PR70 |
| Pairing session | Private setup/verify/result lifetime, immutable options and typed data preparation | PR71 |
| Monitor/listener/companion | Owned worker/resource lifecycle at each boundary | PR45, PR47–48 |

These PRs are merged, independently reviewed, and passed the four AMD64 CI
profiles. v6.1.16 was published with the verified AMD64 package and checksum.
Maintenance/refactor-only PRs did not independently require a version bump.
Latest local and hosted evidence is summarized in [VALIDATION.md](../VALIDATION.md).
This documentation refresh does not republish the existing v6.1.16 binary assets.

## Work still required

- Serialize repeated pairing setup/channel preparation against active event IO;
  the inherited cipher-borrowing gap remains.
- Complete cooperative stopping for RTSP conversations, playback, remaining
  RTP/buffered workers and whole-process signal shutdown.
- Encapsulate remaining public SessionState data and remove runtime global
  configuration reads beyond the explicit timing/directory/pairing inputs.
- Continue RTSP/player, output/discovery, shared-memory and subprocess adapter
  decomposition; owned startup settings do not complete infrastructure isolation.
- Complete bundled pairing C migration and remaining genuine/transitional C
  boundaries. The current build still compiles the pairing library and C fixtures.
- Establish real-device realtime/buffered/surround, pause/resume, reconnect,
  Home, pairing/re-pairing and multiroom acceptance.
- Measure CPU, live memory, latency, allocations and underruns under controlled
  equivalent source material, devices, sample formats and load.

Pairing authorization and persistence have not been redesigned. The directory
remains in-memory, and successful local native exchanges do not prove Home
integration. Legacy cancellation fixtures retain the documented TSan seam;
new cooperative event IO/destruction tests use real native IO without it.

## Evidence history

[The Stage 0 inventory](receiver-migration-contracts.md) describes its original
source snapshot. [The migration log](history/receiver-migration-log.md) retains
per-scenario red/green results, old test counts, local log locations, and release
observations. Its old "review/CI pending" statements are historical checkpoints,
not current delivery status. Earlier redesign measurements are in
[the validation log](history/receiver-validation-log.md), and companion evidence
is in [its verification record](../companion/nqptp/VERIFICATION.md).

No device installation, service restart or performance run was performed as
part of the recent component deliveries or this documentation update. Historical
installed-device observations remain explicitly scoped to their recorded version.

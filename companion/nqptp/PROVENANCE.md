# Bundled NQPTP

Origin: [NQPTP 1.2.8](https://github.com/mikebrady/nqptp/tree/1.2.8),
commit `c925f27c1fd12e4033ac477e5a405969b0b0260b`.
Implementation and required headers are committed here; builds do not fetch
upstream source. Copyright notices and GPLv2 terms are retained in each imported
file and in COPYING, LICENSE and AUTHORS.

Adaptations: CMake replaces Autotools; production source compiles as C++26,
companion symbols use the `nqptp` namespace, allocations have explicit types,
the clock identity buffer has a constant bound, and the entry point is separate.
The receiver's `src/timing/nqptp-shm-structures.h` is the canonical C-compatible
SMI10 contract, supplied through `nqptp-ipc`; there is no companion copy.
Both executables report the monorepo release version, while companion output
also identifies NQPTP 1.2.8 and smi10.

The runtime replaces upstream `exit`/`atexit` cleanup with explicit ownership
of sockets, mapping, descriptor and exclusively created shared-memory object.
Signals are blocked during startup and consumed through signalfd, avoiding
logging or cleanup from asynchronous signal handlers. Binding any required
supported-family address fails startup on conflict. Endpoint and mapping-call
injection is internal to tests; the executable keeps the upstream endpoints.
Unused legacy per-client mappings and the old socket-opening helper were removed.

Packet handlers retain upstream timing arithmetic, first-peer selection and
awakening announcements. Repeated T commands for the same first peer preserve
clock smoothing and mastership while timing is active or its existing validity
period has not expired. They do not extend that validity period. Expired clocks,
changed first peers and T commands without peers retain upstream reset semantics.
Boundary changes reject malformed control before mutation, check signed lengths
before packet access, copy wire
messages to typed packed objects, and remove unchecked optional TLV diagnostics.
Wire layouts use platform packing pragmas with standard fixed-size members,
replacing zero-length array members. There is no GNU language-extension mode.

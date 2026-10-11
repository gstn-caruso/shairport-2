# AirPlay 2 receiver

The receiver uses a single AirPlay 2 RTSP handler table. It preserves setup, record, teardown, GET/POST pairing and Home endpoints, rate/anchor updates, buffered flush, realtime flush, GET_PARAMETER and SET_PARAMETER volume/progress/metadata handling. ANNOUNCE and unsupported RTSP methods return 501. NTP setup returns 400; streaming clocks use PTP through NQPTP.

Realtime ALAC 44.1 kHz / 16-bit stereo uses FFmpeg and 352-frame packets. Buffered decoding retains ALAC 48 kHz / 24-bit stereo and AAC stereo, 5.1 and 7.1 formats. PulseAudio negotiates the destination format and channel layout.

Avahi advertises both `_airplay._tcp` and the complementary `_raop._tcp` records used by AirPlay 2 discovery. The RAOP record does not enable an AirPlay 1 handler or fallback. Pairing and encrypted sessions retain libgcrypt, libsodium and OpenSSL.

NQPTP is validated before opening the RTSP listener or publishing discovery records. Missing/read-inaccessible shared memory, short mappings, zero version and mismatched interface versions are fatal errors. The version expected by this receiver is declared in `nqptp-shm-structures.h`.

The packaged companion is `shairport-sync-nqptp`, using SMI10 and the same
release version as the receiver. [BUILD.md](BUILD.md) describes activation and
restart ordering.

RTSP framing/parameters, buffered/realtime/control reception, pairing management
and encrypted IO now have independent components with explicit ports. Pairing
session options and directory ownership flow from the application to accepted
sessions; algorithms and authorization policy are unchanged. See
[architecture](docs/architecture.md) for modules and resource lifetimes, and
[VALIDATION.md](VALIDATION.md) for real local transcript coverage and remaining
device, re-pairing, and multiroom acceptance.

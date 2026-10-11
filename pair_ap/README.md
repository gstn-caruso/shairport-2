# Embedded AirPlay pairing

The receiver builds `pair.c`, `pair_fruit.c`, `pair_homekit.c` and `pair-tlv.c` for its AirPlay 2 pairing endpoints. This fork fixes the pairing crypto implementation to libgcrypt with libsodium; the receiver's cryptography also requires OpenSSL. Standalone example clients/servers and alternate pairing crypto implementations are not included.

Original authorship and license notices remain in each source file.

Receiver ownership is implemented outside this C library. `PairingSession`
owns setup/verify contexts and keeps native results private until context
release; `EncryptedChannel` owns cipher state and buffered byte IO. Management
callbacks use an explicitly supplied `PairingDirectoryPort` and owned snapshots.
These independently linked C++ components retain the native channel numbers,
framing and algorithms. See [architecture](../docs/architecture.md#pairing-and-encrypted-io)
and [validation](../VALIDATION.md) for current contracts and limits.

Bundled C migration, authorization/persistence redesign and complete live
re-pairing acceptance remain open. Do not interpret the native context API's
borrowed result as independently owned storage.

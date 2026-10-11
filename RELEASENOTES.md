# Releases and migration

Current fork releases are generated in [CHANGELOG.md](CHANGELOG.md) and published
as [GitHub Releases](https://github.com/gstn-caruso/shairport-sync/releases).
The verified documentation baseline is **v6.1.16**. Both receiver and bundled
timing companion use the same release version.

## Upgrading this fork

Version 6.0.0 replaced the legacy receiver CLI with foreground operation,
`--config PATH`, `--version`, and `--check-config`. Removed short, daemon/control
and setting flags are errors. Follow [CONFIGURATION.md](CONFIGURATION.md) for
configuration equivalents and [BUILD.md](BUILD.md) for package/service setup.

Version 6.1.0 bundled the `shairport-sync-nqptp` companion. Stop an older timing
service before activating the bundled one, and restart the receiver after a
companion restart; see [timing migration](BUILD.md#migrating-a-separately-installed-nqptp).

Subsequent releases through v6.1.16 deliver independently tested components and
targeted correctness fixes. The [architecture](docs/architecture.md),
[migration status](docs/receiver-migration-progress.md) and
[validation](VALIDATION.md) distinguish merged work from remaining shutdown,
re-pairing, device and performance acceptance. Documentation/refactor-only changes
do not create a release by themselves; automation selects the largest required
Conventional Commit bump.

## Historical upstream notes

[Upstream release history](docs/history/upstream-release-notes.md) is retained
for provenance. Its backend, platform, CLI and feature descriptions apply to
those historical upstream releases, not the supported Linux/AirPlay 2/PulseAudio
stack of this fork. Use the current guides above when configuring this version.

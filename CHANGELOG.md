## [7.0.0](https://github.com/gstn-caruso/shairport-2/compare/v6.1.17...v7.0.0) (2026-10-11)

### ⚠ BREAKING CHANGES

* Executables, Debian package, configuration defaults and systemd units now use shairport-2 instead of shairport-sync. Existing installations must follow the migration guide before activating the renamed services.

### Features

* rename project and programs to shairport-2 ([#75](https://github.com/gstn-caruso/shairport-2/issues/75)) ([4a9ef4d](https://github.com/gstn-caruso/shairport-2/commit/4a9ef4dea16dd7d1d6fb4fcf4f65433b7e22f4c7))

## [6.1.17](https://github.com/gstn-caruso/shairport-2/compare/v6.1.16...v6.1.17) (2026-10-11)

### Bug Fixes

* **nqptp:** reuse live timing peer across playback setup ([#74](https://github.com/gstn-caruso/shairport-2/issues/74)) ([51403c2](https://github.com/gstn-caruso/shairport-2/commit/51403c20651fb9e2d10217dc8c97c08320b389a4))

### Documentation

* refresh current guides and install complete documentation bundle ([#72](https://github.com/gstn-caruso/shairport-2/issues/72)) ([590f09e](https://github.com/gstn-caruso/shairport-2/commit/590f09ef835c915912e8e2bb911ef99b0c74e616))
* simplify project guides and remove historical fork records ([#73](https://github.com/gstn-caruso/shairport-2/issues/73)) ([81274dc](https://github.com/gstn-caruso/shairport-2/commit/81274dc23c1332442ff23478efe9d989845719cd))

## [6.1.16](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.15...v6.1.16) (2026-10-11)

### Bug Fixes

* **pairing:** own session contexts and guard data channel preparation ([#71](https://github.com/gstn-caruso/shairport-sync/issues/71)) ([1a06ea5](https://github.com/gstn-caruso/shairport-sync/commit/1a06ea54942ba9b9592269128f1ae68650d2082b))

## [6.1.15](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.14...v6.1.15) (2026-10-11)

### Bug Fixes

* **pairing:** own shared directory and reject invalid public keys safely ([#70](https://github.com/gstn-caruso/shairport-sync/issues/70)) ([21baa9f](https://github.com/gstn-caruso/shairport-sync/commit/21baa9fae989b1225a78dc17cbbb167e963a2d17))

## [6.1.14](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.13...v6.1.14) (2026-10-11)

### Bug Fixes

* **pairing:** own encrypted channel state and release failed outputs safely ([#69](https://github.com/gstn-caruso/shairport-sync/issues/69)) ([14109b2](https://github.com/gstn-caruso/shairport-sync/commit/14109b273f76302cced4871736b4a554eacc93b1))

## [6.1.13](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.12...v6.1.13) (2026-10-11)

### Bug Fixes

* **ap2:** own event listener lifecycle and stop cooperatively ([#68](https://github.com/gstn-caruso/shairport-sync/issues/68)) ([187b1b3](https://github.com/gstn-caruso/shairport-sync/commit/187b1b3dee93def87fb42eba35fa3f96e4ae9cf8))

## [6.1.12](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.11...v6.1.12) (2026-10-11)

### Bug Fixes

* **ap2:** bound event replies in an owned command exchange ([#67](https://github.com/gstn-caruso/shairport-sync/issues/67)) ([dd70bdb](https://github.com/gstn-caruso/shairport-sync/commit/dd70bdb72f7029962b9deffd745ad94af9c7c91b))

## [6.1.11](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.10...v6.1.11) (2026-10-11)

### Bug Fixes

* **transport:** own control UDP lifetime and wake blocked operations ([#66](https://github.com/gstn-caruso/shairport-sync/issues/66)) ([06a4799](https://github.com/gstn-caruso/shairport-sync/commit/06a4799c6dd6369f9a78a98663d198e5cf2dfacf))

## [6.1.10](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.9...v6.1.10) (2026-10-11)

### Bug Fixes

* **rtp:** own resend transactions and snapshot control peers ([#65](https://github.com/gstn-caruso/shairport-sync/issues/65)) ([b74fa71](https://github.com/gstn-caruso/shairport-sync/commit/b74fa7112ae355893985dc498b636c8ef34ab608))

## [6.1.9](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.8...v6.1.9) (2026-10-11)

### Bug Fixes

* **timing:** own shared RTP settings and eliminate offset races ([#64](https://github.com/gstn-caruso/shairport-sync/issues/64)) ([964167c](https://github.com/gstn-caruso/shairport-sync/commit/964167c6f5c1a4ab2aaea58fd7242f78f045b0d2))

## [6.1.8](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.7...v6.1.8) (2026-10-11)

### Bug Fixes

* **rtp:** bound latency arithmetic in an isolated control coordinator ([#63](https://github.com/gstn-caruso/shairport-sync/issues/63)) ([cc2702e](https://github.com/gstn-caruso/shairport-sync/commit/cc2702eaa015de1467524d72c200e384acf82ee2))

## [6.1.7](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.6...v6.1.7) (2026-10-11)

### Bug Fixes

* **rtp:** reject unauthenticated audio through explicit realtime ingress ([#62](https://github.com/gstn-caruso/shairport-sync/issues/62)) ([0ac93a0](https://github.com/gstn-caruso/shairport-sync/commit/0ac93a09e1fa33d64c6dcf836420a7a200fee989))

### Code Refactoring

* **ap2:** coordinate buffered reception through explicit ports ([#61](https://github.com/gstn-caruso/shairport-sync/issues/61)) ([78c4484](https://github.com/gstn-caruso/shairport-sync/commit/78c448438204b09e1e28d94f4721727b821e13f2))

## [6.1.6](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.5...v6.1.6) (2026-10-11)

### Bug Fixes

* **ap2:** prevent timestamp overflow in buffered playback policy ([#60](https://github.com/gstn-caruso/shairport-sync/issues/60)) ([ed47033](https://github.com/gstn-caruso/shairport-sync/commit/ed47033389e314ea1f770410fc9e94e365ca1777))

## [6.1.5](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.4...v6.1.5) (2026-10-11)

### Bug Fixes

* **transport:** preserve buffered TCP terminals and own worker shutdown ([#59](https://github.com/gstn-caruso/shairport-sync/issues/59)) ([e457d99](https://github.com/gstn-caruso/shairport-sync/commit/e457d996d5a77e595b3a55b113f33b12c4cf21ae))

## [6.1.4](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.3...v6.1.4) (2026-10-11)

### Bug Fixes

* **ap2:** validate buffered blocks before owned payload preparation ([#58](https://github.com/gstn-caruso/shairport-sync/issues/58)) ([135b252](https://github.com/gstn-caruso/shairport-sync/commit/135b25252ae0658a2117c578c661f996a94fed0a))

### Code Refactoring

* **ap2:** own buffered flush requests and packet decisions ([#57](https://github.com/gstn-caruso/shairport-sync/issues/57)) ([b26dff8](https://github.com/gstn-caruso/shairport-sync/commit/b26dff8b7e935512918674eaab5312c5574ad301))

## [6.1.3](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.2...v6.1.3) (2026-10-11)

### Bug Fixes

* **rtsp:** recover allocation failures in an isolated request reader ([#56](https://github.com/gstn-caruso/shairport-sync/issues/56)) ([1ce0e9e](https://github.com/gstn-caruso/shairport-sync/commit/1ce0e9eb92c4a3f6a46c6bd697ae918ab0cc0d80))

### Code Refactoring

* **audio:** isolate decoder and resampler dependencies ([#50](https://github.com/gstn-caruso/shairport-sync/issues/50)) ([aaa0e26](https://github.com/gstn-caruso/shairport-sync/commit/aaa0e26fe4afa942229f8d9ad54de6d71587a046))
* **config:** own settings and isolate configuration loading ([#53](https://github.com/gstn-caruso/shairport-sync/issues/53)) ([4bdb5f5](https://github.com/gstn-caruso/shairport-sync/commit/4bdb5f5c7873ae19019db3248250a59af37df847))
* **playback:** make output setup dependencies explicit ([#52](https://github.com/gstn-caruso/shairport-sync/issues/52)) ([a5891a3](https://github.com/gstn-caruso/shairport-sync/commit/a5891a3ddb49cc86ad29c68d9b4be9e435864f42))
* **rtsp:** isolate parameter handling behind an explicit volume port ([#51](https://github.com/gstn-caruso/shairport-sync/issues/51)) ([86293f4](https://github.com/gstn-caruso/shairport-sync/commit/86293f4dbb4c4cad0d7973e479469d4d94fb88f5))
* **session:** isolate principal selection behind a C++ module ([#55](https://github.com/gstn-caruso/shairport-sync/issues/55)) ([6d3289c](https://github.com/gstn-caruso/shairport-sync/commit/6d3289c5eb5d9ebef1622104df50e878cb58ddcb))
* **session:** isolate registry lifecycle policy from runtime workers ([#54](https://github.com/gstn-caruso/shairport-sync/issues/54)) ([2464896](https://github.com/gstn-caruso/shairport-sync/commit/2464896ae39f533a9381f59d7b5b255c0ae2820e))

## [6.1.2](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.1...v6.1.2) (2026-10-10)

### Bug Fixes

* **output:** start playback promptly through an owned PCM queue ([#49](https://github.com/gstn-caruso/shairport-sync/issues/49)) ([972deab](https://github.com/gstn-caruso/shairport-sync/commit/972deab06755afcfb07dc4ff4ad89cb0fb012f74))

### Code Refactoring

* **rtsp:** stop listener through its owned lifecycle ([#48](https://github.com/gstn-caruso/shairport-sync/issues/48)) ([6c57893](https://github.com/gstn-caruso/shairport-sync/commit/6c57893a55f4c67d6500f883dff251b3f7acf15c))

## [6.1.1](https://github.com/gstn-caruso/shairport-sync/compare/v6.1.0...v6.1.1) (2026-10-10)

### Bug Fixes

* **activity:** cooperatively stop and restart the monitor ([3ade497](https://github.com/gstn-caruso/shairport-sync/commit/3ade497619b8230967281dae071de4fc370b945e))

### Documentation

* describe integrated timing companion by its role ([#46](https://github.com/gstn-caruso/shairport-sync/issues/46)) ([ef5ea3e](https://github.com/gstn-caruso/shairport-sync/commit/ef5ea3e07ad1108cf91a0f3539fa136ae573206e))

## [6.1.0](https://github.com/gstn-caruso/shairport-sync/compare/v6.0.1...v6.1.0) (2026-10-10)

### Features

* **nqptp:** bundle timing companion in receiver releases ([#45](https://github.com/gstn-caruso/shairport-sync/issues/45)) ([95e80a7](https://github.com/gstn-caruso/shairport-sync/commit/95e80a734050aa775f5c1ec64276dc4821617bd7))

### Documentation

* record delivered C++26 migration gates and release evidence ([51e240f](https://github.com/gstn-caruso/shairport-sync/commit/51e240fd4c0137f3c787c8162d4e32f796ce5463))

## [6.0.1](https://github.com/gstn-caruso/shairport-sync/compare/v6.0.0...v6.0.1) (2026-10-10)

### Bug Fixes

* **pairing:** reserve headers for empty TLV values ([4a3c585](https://github.com/gstn-caruso/shairport-sync/commit/4a3c585c9e8b0f1af059ef4eaef82eac45bd743b))

### Code Refactoring

* **volume:** distinguish wire levels attenuation and PCM gain ([35a27e2](https://github.com/gstn-caruso/shairport-sync/commit/35a27e217a5bbdc7df374fb70324df3208721e9a))

### Tests

* **audio:** expose packet admission and flush outcomes ([#39](https://github.com/gstn-caruso/shairport-sync/issues/39)) ([56eee8e](https://github.com/gstn-caruso/shairport-sync/commit/56eee8e0e7a992d95064da74648fd0ba40e3c39b))
* **player:** clarify playback and cancellation contracts ([#41](https://github.com/gstn-caruso/shairport-sync/issues/41)) ([558b990](https://github.com/gstn-caruso/shairport-sync/commit/558b9907d2c4d6bb675ab4f9b2943bf3edabd3d7))
* **runtime:** clarify volume effects and owned resource cleanup ([#40](https://github.com/gstn-caruso/shairport-sync/issues/40)) ([37237d5](https://github.com/gstn-caruso/shairport-sync/commit/37237d51516c7b1405c747c7e443dac27b60c3ef))

## [6.0.0](https://github.com/gstn-caruso/shairport-sync/compare/v5.7.2...v6.0.0) (2026-10-10)

### ⚠ BREAKING CHANGES

* **app:** only --config PATH, --version, and --check-config are accepted. Move receiver setting flags to their configuration equivalents; systemd owns foreground process supervision.

* fix(config): reject output channel maps exceeding storage

* fix(config): validate setting types and default file boundaries

* fix(audio): release PulseAudio resources after failed startup

* ci: validate packaged receiver configuration without services

* fix(config): validate fixed latency before starting services

* fix(config): reject integers that cannot survive conversion

* fix(config): fail when an explicit file cannot be resolved

### Features

* **app:** introduce foreground daemon interface ([#38](https://github.com/gstn-caruso/shairport-sync/issues/38)) ([40e21db](https://github.com/gstn-caruso/shairport-sync/commit/40e21db0c8e429878f1fecfdff6c7e582ccd5d21))

### Documentation

* establish receiver migration contracts ([#30](https://github.com/gstn-caruso/shairport-sync/issues/30)) ([355c936](https://github.com/gstn-caruso/shairport-sync/commit/355c9364b7ba5467222538e766ede52b69f7dc9d))

### Tests

* **audio:** clarify sample and volume contracts ([#34](https://github.com/gstn-caruso/shairport-sync/issues/34)) ([dcfb21a](https://github.com/gstn-caruso/shairport-sync/commit/dcfb21af57fd71141c0c170d3ccac254e1e0b633))
* **audio:** expose resampler samples and ownership outcomes ([#36](https://github.com/gstn-caruso/shairport-sync/issues/36)) ([bffa071](https://github.com/gstn-caruso/shairport-sync/commit/bffa07163399a83ec73bf3005f3f8ae2a128bcde))
* **audio:** express decoder formats and failures directly ([#35](https://github.com/gstn-caruso/shairport-sync/issues/35)) ([0b0aa0d](https://github.com/gstn-caruso/shairport-sync/commit/0b0aa0d7a4a50fc46c0df3d42165f45e84a40187))
* **rtsp:** express independent dispatch scenarios ([#31](https://github.com/gstn-caruso/shairport-sync/issues/31)) ([830b235](https://github.com/gstn-caruso/shairport-sync/commit/830b235499a23efd0fa92500110de426bad33af6))
* **rtsp:** separate framing and parsing contracts ([#33](https://github.com/gstn-caruso/shairport-sync/issues/33)) ([84c26bc](https://github.com/gstn-caruso/shairport-sync/commit/84c26bc5d4980a52854926fee0f28514ea74803b))
* **session:** expose ownership and shutdown contracts ([#37](https://github.com/gstn-caruso/shairport-sync/issues/37)) ([584d2f9](https://github.com/gstn-caruso/shairport-sync/commit/584d2f9717994484720aa2b59e22545265e7cb9a))

### Continuous Integration

* suspend ARM64 builds and release artifacts ([#29](https://github.com/gstn-caruso/shairport-sync/issues/29)) ([dbe218e](https://github.com/gstn-caruso/shairport-sync/commit/dbe218e4422ee31343d44a5aaaf0ab55a0d7bf63))

## [5.7.2](https://github.com/gstn-caruso/shairport-sync/compare/v5.7.1...v5.7.2) (2026-10-10)

### Bug Fixes

* **release:** honor conventional commit version policy ([#28](https://github.com/gstn-caruso/shairport-sync/issues/28)) ([8822f21](https://github.com/gstn-caruso/shairport-sync/commit/8822f217334fb1cf0eedbef7c7d83a84c6ba9d33))

## [5.7.1](https://github.com/gstn-caruso/shairport-sync/compare/v5.7.0...v5.7.1) (2026-10-10)

### Code Refactoring

* **audio-format:** delegate channel mapping decisions ([#27](https://github.com/gstn-caruso/shairport-sync/issues/27)) ([8b495a7](https://github.com/gstn-caruso/shairport-sync/commit/8b495a755c757f904964d826944f56b2f3277548))
* **volume:** delegate attenuation range decisions ([#26](https://github.com/gstn-caruso/shairport-sync/issues/26)) ([fd1ca7b](https://github.com/gstn-caruso/shairport-sync/commit/fd1ca7bd333de7c000b0ea8629fc354600062408))

## [5.7.0](https://github.com/gstn-caruso/shairport-sync/compare/v5.6.0...v5.7.0) (2026-10-10)

### Features

* **release:** publish native arm64 Debian packages alongside amd64 ([#25](https://github.com/gstn-caruso/shairport-sync/issues/25)) ([764f7b3](https://github.com/gstn-caruso/shairport-sync/commit/764f7b3b1c69810da91e8cae6ddcc275b94cf417))

## [5.6.0](https://github.com/gstn-caruso/shairport-sync/compare/v5.5.1...v5.6.0) (2026-10-10)

### Features

* **release:** publish versioned Debian packages after validated pushes ([#23](https://github.com/gstn-caruso/shairport-sync/issues/23)) ([1438584](https://github.com/gstn-caruso/shairport-sync/commit/1438584317b0c242eb25cb13240bfcccbc88ca18))

### Bug Fixes

* **release:** pin changelog preset compatible with notes writer ([#24](https://github.com/gstn-caruso/shairport-sync/issues/24)) ([3a0fbc9](https://github.com/gstn-caruso/shairport-sync/commit/3a0fbc9a342501b24056d1fdf756e043e44aa56e))

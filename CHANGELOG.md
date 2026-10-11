# Changelog

## Unreleased

### Added

- Add the SFZ sampler of S9 on the audaudio fork of sfizz

- aud.sampler.sfz on ABI 0.4: loads on an engine worker with ids, progress and cancellation, a standby synth adopted with a 5 ms crossfade and the controllers replayed, failed loads keep the instrument

- Six parameters with stable ids; polyphony and tuning reload on a worker; UMP of MIDI 1.0 and 2.0 with poly pressure, numeric controls as CCs, messages to sfizz's message API with the answers through the engine

- The fork: one sample store per process shared by every synth, streams as engine tasks, trims to the sample cache budget, silence and an underflow report when a stream is late, AUD_ERROR_CAPACITY for heads beyond the budget

- The fork: no allocation on the audio thread (messages, tuning, debug output), shared tables set once, an atomic random generator, the samples and effects a load could not use in its message, RBJ filters from the Audio EQ Cookbook, fverb, the metronome and cephes gone

- A new synth restores the controllers without starting CC-triggered regions; an unreadable stream is not asked for again; a reload rebuilds the instrument that plays

- NOTICES with the fork's origin and the STK-4.3 notice; scripts/vendor-sfizz.js retired

- Dart: AudSfzSampler, AudSfzPreset, AudDspSampler with @RecordUse members and a link hook that drops the library of an app that does not use it, AudSfzTestInstrument in testing.dart

- Tests: native tests with the engine under ASan/UBSan, RealtimeSanitizer with a probe and ThreadSanitizer, the RBJ filters against the cookbook and the Faust originals, golden renders of the test instrument, Dart tests

- Add the core 0.4 constraint the sampler builds on

- aud_audio_core ^0.4.0 instead of ^0.1.0, as the graph of the dev dependencies requires; pub upgrade --tighten failed on the old range

- Add per-filter RBJ tests and a stricter native harness

- RBJ filter tests: one test per filter and aspect, checked with AUD_CHECK and AUD_CHECK_NEAR, silent when they pass, in the repo's 2-space style with // .... landmarks
- scripts/test-native.js refuses unknown options and --only values with the usage and exit code 2, passes the pristine sfizz path as an escaped C string and prunes the objects and binaries of earlier runs
- WorkerHold holds every worker of the engine and checks each post

## 0.1.0 - 2026-10-08

### Added

- Add the sfizz sampler node
- Add the sfizz vendoring script

### Changed

- Port the vendoring script to Node.js

## 0.0.2 - 2026-10-08

### Added

- Initial boilerplate

### Changed

- Record the gg commit state
- Set up GitHub repo settings and branch rules
- Update dev dependencies

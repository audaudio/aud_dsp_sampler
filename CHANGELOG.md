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

- Add safe streams, refused headers and notices to the fork

- A stream reports its bytes before its status lets a trim free the file; the store's report takes the host by value

- A sample with more frames than a voice counts or more channels than sfizz plays is refused, by the load and by the stream; buffers sized from a header allocate without throwing, so a load drops the sample and a stream leaves the voices on the head instead of ending the process

- A pool without a host reads whole files at once

- ScopedFTZ restores the control register on ARM; the RBJ filters keep the cutoff below Nyquist; a load keeps the first 16 errors of its parser

- Dead code gone: RTSemaphore, SpinMutex, threadpool and atomic_queue with their SOURCES, INCLUDES and NOTICES lines, the fverb opcode specs, the pool's empty stubs and the garbage collection timer, FileInformation::maxOffset, two config constants, the beat clock buffers only the metronome read

- The Modified by Audanika notes below the license headers in one form, also in Effects.h and the other files changed now

- NOTICES: RbjFilters.hpp as Audanika's MIT code apart from sfizz, the strings effect under STK-4.3, decisions license-002 and license-003, the exempt helpers; LICENSE-STK-4.3.md with the copyright notices of the Faust library functions

- The native tests of the fork compile as gnu++14 like the fork, the fixture in both language versions; tests for the order of a stream, streams while trims run, refused and oversized headers, pools without a host, flush-to-zero, parse errors and cutoffs above Nyquist

- Add race-free reloads and early controllers to the node

- A load and a reload agree on which one is newer: the count of the loads, the deferred reload and the serial change under the mutex
- Before the first instrument the node ramps the volume, remembers the controllers for it and counts the messages nothing answers
- A release beyond what a tail counts reports the longest finite tail
- create refuses a host that lacks a function the node calls, or whose engine takes no more caches, and logs why; no exception crosses the C ABI from create, prepare, set_string or a load
- Stepped parameters round to their step; the parameter table is defined once, the defaults and the crossfade derive from it; one helper posts the tasks, one template reloads on a change; the include groups are separated
- Native tests for the host checks, a load that starts with a reload, a polyphony change during a load, the volume and the controllers before the first instrument, the controllers of MIDI 1.0 and 2.0, programs, numeric controls, refused messages, the tempo of the transport, a Scala file, the stretch tuning, the quality steps and releases beyond any tail

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

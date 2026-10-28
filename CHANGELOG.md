# Changelog

## 0.5.0 - 2026-10-28

### Changed

- Require Dart 3.13 and upgrade the dependencies

- Require Dart 3.13

- Upgrade dna_audanika to ^0.1.0

- Upgrade helix to ^1.14.0

- Set the identity the DNA layers ask for in dna/_vars.json

- Reformat 4 files with the Dart 3.13 formatter

- Add the blog post of this change, in English and German

- Bring the README to the README template, in English and German

- Upgrade aud_midi_standard to 0.2.0

- Lift the lock file to the newest resolvable versions

- Resolve the example together with the package

- Add the example to a pub workspace, resolution: workspace in the example

- Drop the lock file of the example, the package lock covers it

- Align the example constraints and the test constraint with the workspace

## 0.4.0 - 2026-10-24

### Changed

- Build and test the sampler on Windows
- Document the Windows support
- Document the Windows behavior in both languages
- Stop relinking the sampler on every Windows build

## 0.3.5 - 2026-10-22

## 0.3.4 - 2026-10-19

### Added

- Add note_offset_open_bounds and fail on missing includes

- The fork of sfizz fails a load whose #include names a file it cannot read, even beside regions that load; Synth::getMissingIncludes() names them relative to the folder of the SFZ file, Parser::getMissingIncludes() lexically normal

- The node's load message names them: "sfizz could not read 1 include: ../../samplesets/x/regions.sfz"

- note_offset_open_bounds in <control>: under note_offset and octave_offset a region that reached key 0 or 127 still reaches it; off by default, the goldens render the same bit for bit

- Native tests of presets that include a sample set from files, apk:/ assets and SFZ text with a virtual path: includes relative to the root file, $ variables in include paths and opcode values, default_path and its reset by a second <control>, note_offset of -12 and +12 with and without the opcode, missing includes

- FLAC samples that the tests write render like the WAV of their frames, with the loop from the SFZ file or from a smpl chunk

- A Dart test of the message of a missing include; the README, NOTICES.md, the C header and the Dart docs describe the layout, the opcode and the failure

- Add one parse check and shared helpers for the tests

- Synth::Impl::parseLoads() holds what loadSfzFile() and loadSfzString() both checked after a parse: a missing include fails the load, an instrument without regions only without parser errors

- The FLAC writer of the native tests in named steps: FlacBits with its CRC, STREAMINFO, the smpl loop, a frame; the files it writes stay byte for byte the same

- kLevel and since() move into the test fixture; the sample set test builds its loop opcodes from kLoopStart and kLoopEnd and compares the paths the backend opened as whole lists

## 0.3.3 - 2026-10-15

### Added

- Add a native bench of the voice loop by interpolation, envelope and voice start

- The cost per voice and the fixed part of a block with 1, 8 and 32 held notes, by interpolation quality, flex EG or ADSR, mono or stereo, with and without pitch bend

- The extra of a block that starts a burst of 10 note-ons

- Renders of every variant compared within 1e-5 of full scale (sampler-007) against release 0.3.2

- --bench --android builds both forks with the NDK and runs them on each CPU cluster of a device

- Add a walk over the busy voices only, per block and per event

- The voice manager keeps a bit per voice that is not idle; the render loop and the loops of note-offs, controllers, pitch bend, aftertouch, off groups, all notes off and all sound off visit only those, in the order of the list

- findFreeVoice() takes the first idle voice without a walk

- The renders stay the same bit for bit

- Add fused amplitude and pan passes of a voice

- The gains of the amplitude stage take one pass; gain, pan, width, position and compensation take one more when they hold over the block (panMono(), panStereo())

- A region without crossfade controllers skips the crossfade; the power follower runs only for the stealer that reads it

- A modulation target with one source at depth 1 hands out the source buffer instead of a copy, a cycle keeps the copy

- The stages of a voice are no longer timed for the callback breakdown

- The renders stay the same bit for bit

- Add interpolation of four frames at once and skip the walks of a block

- A pitch that holds over the block converts once; the positions are summed and split into indices and coefficients in one pass where cumsum() has no SIMD code

- A block that ends before its loop, its loop crossfade or its sample skips the walks that wrap and end

- On arm64 the Hermite interpolation computes four frames in a vector with the products and sums of a frame

- The renders stay the same bit for bit

- Add envelope stages rendered as a whole

- A sustain of the flex EG past its stage time fills its level; the other stages sum their times first and then take their levels

- stepSum() sums a step as a loop of float additions does, bit for bit, mostly in vector runs

- The sustain of the ADSR fills its level

- The renders stay the same bit for bit

- Add a voice start without walks, lookups and clock reads

- A polyphony check over fewer candidates than its limit finds no voice to steal without walking them

- A layer keeps the modulation targets of its region instead of nine lookups per start; a file identifier hashes its name once

- The callback breakdown times blocks and events only once it was asked for

- The renders and the voices stay the same bit for bit

- Add the levels of a flex EG stage four frames at once

- On arm64 the levels of a timed stage are computed for four frames at once, which mostly share the two points of the curve

- A block that ends before its stage skips the search for the end; stepSum() takes sums below 16 steps in a plain loop

- The renders stay the same bit for bit

- Add the faster voice loop to the README and the index

- The goals and the documentation name sampler-009 and the plan of ticket 30

- Add the fixes of the review of ticket 30

- getModulation() hands out const buffers; native tests prove the single-source shortcut, the copy before a further source and the cycle against the computation of before

- Named gains of the pan compensation, one loop of the amplitude gains, splitPosition() shared with the interpolation cast

- The callback breakdown no longer adds the stage times that stay 0, and its docs say when it times

- The stepSum() test lives in sfizz_simd_helpers_test.cpp; the bench picks its configs by name

- test-native.js finds the Android SDK and NDK through ANDROID_HOME, ANDROID_SDK_ROOT, ANDROID_NDK_HOME and ANDROID_NDK_ROOT; the script test uses no real device serial

- The German README names the interpolation and the envelope

## 0.3.2 - 2026-10-14

### Added

- Add a real-time voice limit and one voice budget per process

- max_voices (1 to 256, default 256) takes effect at the next block without a reload

- One voice budget for the live renders of all sampler nodes of the process, with a reserve of free voices (4 by default)

- The excess goes at the end of each block: released voices first, the oldest first, all layers of a note together, with a linear fade of 10 ms

- The excess over the budget is shared out by the released, then the held voices of each node

- C API aud_dsp_sampler_set_voice_budget, _voice_budget, _set_voice_reserve, _voice_reserve and _voice_stats with AudSfzVoiceStats

- AudDspSampler.voiceBudget, voiceReserve and voiceStats, AudSfzVoiceStats, AudSfzSampler.maxVoices and the preset argument maxVoices

- Voice::steal, countVoices and stealVoices of VoiceManager and Synth, and VoiceCount.h in the sfizz fork

- Native tests of the limit, the fade, the steal order and the budget across nodes, graphs and threads; Dart tests

- aud_midi_standard from pub.dev, as aud_audio_graph and aud_audio_core take it

- The voices in the READMEs, the C header, NOTICES.md and index.jsonc

- Add the withdrawal of the voices of a node that renders no output

- A node processed without an output bus no longer keeps its last voices in the budget of the process

- Add the constraints aud_audio_core ^0.5.1 and aud_audio_graph ^0.5.1

- Add the fixes of the review of the voice budget

- The stolen voices count in live blocks; a node without an output withdraws its voices (READMEs, header, AudSfzVoiceStats)

- The voices are kept in each block after its events, as the texts now say

- sampler-008 in the goals and the decisions of the READMEs

- voiceStats checks the result in debug builds and frees its struct with an arena

- The steps of max_voices from kNoVoiceLimit; a compile-time check that the voice counters take no lock

- The parameters of stealVoices and fadeOutStolen documented; the equality test of AudSfzVoiceStats as a loop

## 0.3.1 - 2026-10-14

### Added

- Add thread safety to the state all synths of a process share
- A mutex guards the curves of flex envelopes, which parallel loads corrupted (an app crashed on a preset change)
- The silence wavetable is built once and thread-safe
- The Faust table of the distortion is filled once instead of by every stage
- The CRC table of stb_vorbis and the CPU detection of dr_flac and dr_mp3 are set up once
- Regression tests load in parallel threads and on the engine's workers; TSan reports file and line on macOS
- NOTICES.md lists the changed files and the audit of the shared state
- Add the sample rate of the distortion stages to the notes
- A new sample rate sets the constants of each stage; the original kept those of 48 kHz
- Add the review fixes of the shared state notes
- Correct the notes of Disto.cpp and NOTICES.md: a new sample rate set the constants of each stage before too, the distortion sounds as in 0.3.0
- Drop the inner flag of dr_flac's CPU detection, which the once guard makes redundant
- Name the states of the once guards of dr_flac and stb_vorbis instead of 0, 1 and 2
- Let a failed dsymutil leave no dSYM bundle and name its error, also when it is missing
- List AUD_RTSAN_CXX and AUD_LLVM_SYMBOLIZER in the usage of scripts/test-native.js
- Describe the TSan symbolizer on macOS and the shared state of the synths in README.md and README.de.md
- Raise the version in the installation of both READMEs to ^0.3.0
- Add a faster MIDI flush, compressor and gain to the fork
- Flush only the MIDI event vectors that received events since the last flush, from a list of fixed size
- Call log10 in the compressor only above a level 1e-4 below the threshold and pow only for a gain other than 0 dB
- Pass silence through a compressor at rest and skip the effects of a silent bus whose effects are at rest
- Run the oversampling filters and detectors of both compressor channels in one loop on state in registers
- Convert the gain of the gain effect once per block and a voice's pitch and volume modulation once per run of equal values
- Add is_cleared to the oversampling filters of hiir
- Add native tests that compare the MIDI state, the compressor, the gain and the effect bus with the original code bit for bit
- Add the render of an app-like instrument and its golden from release 0.3.0
- Add node scripts/test-native.js --bench, the micro-benchmark against release 0.3.0
- List the changed files in NOTICES.md and describe the changes in README.md and README.de.md

## 0.3.0 - 2026-10-13

### Added

- Add apk:/ paths that read the assets of an Android app
- A file layer under sfizz: VirtualFile.h serves paths with a scheme from a backend
- apk:/ paths read SFZ files, includes, samples and Scala files from the APK through AAssetManager, without a copy
- The Android plugin hands the AssetManager to the native library when the app starts
- The store keys samples by their lexically normal path, so instruments share them
- Native tests with a directory as the assets, the example loads from its APK on the emulator
- README: load instruments from Android assets
- Abseil's int128.cc and int128_no_intrinsic.inc of release 20240116.0: the library builds for armeabi-v7a
- On Android the library links with --no-undefined, so a missing symbol fails the build
- An instrument without regions loads and plays nothing, e.g. a text with only a comment
- The gain effect takes blocks shorter than the block size; comp and gate link the channels on every frame
- A stolen voice with a flex EG for the amplitude fades over off_time instead of its whole release
- sustain_sw and sostenuto_sw each switch their own pedal; they were swapped
- The tail of the node counts the release of a flex EG that replaces the ADSR
- An empty APK asset reads as empty, one beyond the address space of a 32-bit process as unreadable
- Add a clean end to the fade of an empty flex EG
- The fade of a voice turned off ends its flex EG without a stage number of its own, also for an EG without points
- A native test turns off voices with empty flex EGs in every off mode
- Add the fixes of the light review
- Release the global reference of the AssetManager when its backend is refused
- Wrap the README paragraph on loads and end pubspec.yaml with a newline

## 0.2.0 - 2026-10-12

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

- Add local test closures and structural asserts

- The helpers of the Dart tests are closures in the files that use them; test/support is gone

- The golden renders moved into the test of the test instrument they render

- The store test expects at least the files and bytes of its instrument, since the suites of dart test share the store of the process

- One test per member of the link hook and per root key; the C indices, the WAV header and the error of two tunings compared whole

- The link hook names its library in samplerLibrary; a test checks its members against the @RecordUse members of AudDspSampler

- The native harness test runs every test and needs each build to pass all the tests it ran

- Add parameter docs and a factory to the test instrument

- AudSfzTestInstrument.write is a factory constructor, called as before; the private constructor has its doc, the members follow the code guide order

- The rounded sample frequency plays at most 11 cents off, on C2, not 4

- Every named parameter of AudSfzPreset.file, AudSfzPreset.text and AudSfzTestInstrument.preset is documented

- useSampler of the example has a doc comment

- Add the template sections to both READMEs

- README.md follows the readme template: goals, installation, documentation, code examples, how it works, contributing

- It names every public member and links the decisions sampler-001 to sampler-003, lifecycle-002, osc-002 and license-003 of aud_audio_pm

- It describes the native fixes: whole files without a host, refused samples, trusted SFZ input, the options and the build cache of test-native.js

- README.de.md translates it completely, with the same code blocks

- index.jsonc lists the dev dependency aud_midi_standard

- Add safe drops of samples that fail to load to the fork

- A sample whose head, or whole file without a host, the pool cannot load is named as missing and its regions are dropped; the load kept them before, and a note on one read a null modulation buffer on the audio thread

- The load checks the regions, then loads the files, then gives the lists that start voices only to the regions it keeps; a short sample the silence check cannot read whole is dropped instead of read through a null handle

- A dropped region leaves the keyswitch lists and its set as well: sfizz kept pointers to the freed layer there, and a keyswitch note wrote into freed memory

- A voice whose file the pool does not hold goes back to idle at once instead of rendering a block without its modulation targets

- Native tests: samples that do not fit into memory with and without a host, as an oscillator and embedded in the text, a sample gone before its file loads, a short one that turned unreadable, the lists of dropped regions, a voice without its file, the load message of the node; the fixture holds the allocation limit of the sanitizer builds and writeClaimingWav

- README.md and README.de.md: a sample the memory cannot hold is dropped and named as missing

- Add the BSD-2 license to Audanika code in the fork

- RbjFilters.hpp carries the BSD-2-Clause header of sfizz with the copyright of Audanika instead of the MIT header of the package

- NOTICES.md and the READMEs: the noted changes and RbjFilters.hpp are BSD-2-Clause like sfizz, so that the fork has one license

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

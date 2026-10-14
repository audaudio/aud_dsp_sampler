# aud_dsp_sampler

The SFZ sampler of the Audanika Audio Engine: the node `aud.sampler.sfz`
plays SFZ instruments on the engine of `aud_audio`. It loads them on a
worker thread of the engine while the old instrument plays on, shares their
samples between every node of the process within the sample cache budget
and renders on the audio thread without allocating. Built on the audaudio
fork of sfizz.

Part of the Audanika Audio Engine; planned in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## Goals

- SFZ instruments as a node of the engine, played from UMP of MIDI 1.0 and
  2.0 and stored as node presets in graph documents (ticket 25, step S9)
- Loads that block neither Dart nor the audio thread, with ids, progress
  and cancellation; the old instrument plays until the new one swaps in
  with a crossfade of 5 ms (sampler-003)
- One sample store per process within the sample cache budget of the
  engine (lifecycle-002)
- Nothing allocates on the audio thread (interop-001); the native tests
  run the node under the engine's watchdog, the RealtimeSanitizer and the
  ThreadSanitizer
- sfizz's message API on the routes of the node, answered through the
  engine (osc-002)
- An app that does not use the sampler ships without its native library
  (interop-001)
- A fork of sfizz under permissive licenses only (sampler-002,
  license-003)

## Installation

```yaml
dependencies:
  aud_dsp_sampler:
    git:
      url: git@github.com:audaudio/aud_dsp_sampler.git
      tag_pattern: "{{version}}"
    version: ^0.3.0
```

An app depends on `aud_audio` and on the DSP packages it uses, and
registers them with its engine (family-001). The build hook compiles the
node and the fork of sfizz with the C and C++ toolchain of the target; the
package itself needs no Flutter. The native tests need Node.js and
`clang++`; their RealtimeSanitizer build needs a clang that has it, e.g.
LLVM from Homebrew on macOS (`brew install llvm`), and is skipped without
one.

## Documentation

- [The plan of ticket 25](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/tickets/2026-10-11-25-build-the-sfz-sampler-on-sfizz-for-ios-and-android.md)
  with the node, its measurements and findings
- The decisions behind the package:
  - [sampler-001](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-001.md):
    sfizz, forked, as the core of the sampler
  - [sampler-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-002.md):
    the package is the audaudio fork of sfizz
  - [sampler-003](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/sampler-003.md):
    loads into a standby synth, swapped in with a crossfade
  - [lifecycle-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/lifecycle-002.md):
    the resources of the process and the sample cache budget
  - [osc-002](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/osc-002.md):
    messages to nodes and their answers
  - [license-003](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/decisions/license-003.md):
    the audit of the Faust-generated code
- [The sfizz topic](https://github.com/audaudio/aud_audio_pm/blob/main/doc/2026-Q4/concepts/topics/sfizz.md)
  with what ticket 25 found in sfizz
- The C API: [`src/aud_dsp_sampler.h`](src/aud_dsp_sampler.h)
- The components of the fork and their licenses:
  [`src/third_party/NOTICES.md`](src/third_party/NOTICES.md)
- The guides in [`doc/guides`](doc/guides)

## Code Examples

The sampler on the engine of `aud_audio` (`aud_audio_ffi.dart` on native
platforms):

```dart
import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';

Future<void> main() async {
  final engine = AudEngine(
    packages: [
      AudNativeNodePackage(AudDspSampler.name, AudDspSampler.register),
    ],
  );
  engine.prepare();
  final graph = engine.graph;
  final piano = graph.createNode(
    AudSfzSampler.typeId,
    preset: AudSfzPreset.file('/path/to/piano.sfz', polyphony: 32),
  );
  graph.transaction((tx) => tx.connect(piano, graph.io));
  engine.start();

  // The piano plays on until the organ is loaded and swaps in.
  final id = graph.setString(
    piano,
    AudSfzSampler.sfzFile,
    '/path/to/organ.sfz',
  );
  final load = await graph.loadEnded(id);
  print('${load.resultName} ${load.message}'); // AUD_OK, what it could not read
  graph.setParam(piano, AudSfzSampler.volume, -6);
  graph.messages.listen((m) => print('${m.path} ${m.arguments}'));
  graph.sendMessage(piano, AudOscMessage('/num_regions'));
  print('${AudDspSampler.storeFiles} files, ${AudDspSampler.storeBytes} bytes');
}
```

An instrument as SFZ text, the way a preset carries it without a file:

```dart
import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';

void main() {
  final preset = AudSfzPreset.text(
    '<region> sample=c4.wav pitch_keycenter=60',
    virtualPath: '/path/to/sketch.sfz', // c4.wav lies next to it
    name: 'Sketch',
    tuningFrequency: 442,
    scalaFile: '/path/to/werckmeister.scl',
  );
  print(preset.toJson()); // aud_node_preset.schema.json of aud_audio_core
}
```

Tests and examples play the generated test instrument of `testing.dart`,
which ships no sample files (scope-002):

```dart
import 'dart:io';

import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';

void main() {
  final graph = AudGraphFfi(maxFrames: 128, listen: false);
  AudDspSampler.registerWith(graph.hostApi); // throws AudDspSamplerException
  final directory = Directory.systemTemp.createTempSync('aud_instrument_');
  final instrument = AudSfzTestInstrument.write(directory.path);
  final node = graph.createNode(
    AudSfzSampler.typeId,
    preset: instrument.preset(polyphony: 16),
  );
  graph.transaction((tx) => tx.connect(node, graph.io));
  graph.start();
  graph.waitLoads(node: node); // blocks: tests and offline renders
  const note = MidiNoteOn(channel: 0, note: 60, velocity: 100);
  for (final event in AudUmpEvent.fromMessage(note)) {
    graph.sendEvent(node, event);
  }
  final wav = AudOfflineRenderer(graph).renderWav(frames: 48000);
  AudWavFile.write('${directory.path}/c4.wav', wav);
  graph.dispose();
}
```

The example app of `aud_audio` plays the sampler on its sampler page;
`example/` of this package shows the ids of the node and a preset, and
proves the link hook. Its integration test loads an instrument from the
assets of its APK on an Android device:

```bash
cd example && flutter test integration_test/apk_assets_test.dart -d <device>
```

## Load instruments from Android assets

On Android the sampler reads SFZ files and their samples from the assets of
the APK in place, without copying them out, the way the legacy audio engine
of Audanika did. A path that starts with `apk:/` names an asset:

```dart
graph.setString(
  piano,
  AudSfzSampler.sfzFile,
  'apk:/content/sfz/presets/piano/main.sfz', // assets/content/sfz/... in the APK
);
```

- The path after `apk:/` is the path inside the assets of the APK:
  `apk:/content/sfz/x.sfz` for `android/app/src/main/assets/content/sfz/x.sfz`,
  `apk:/flutter_assets/assets/sfz/x.sfz` for the Flutter asset
  `assets/sfz/x.sfz`. The relative paths inside the SFZ file - `sample=`,
  `#include`, `default_path`, with `..` - resolve inside the APK as well,
  and so does a `scala_file` or the `sfz_virtual_path` of an SFZ text.
- Samples are read through `AAssetManager`: an uncompressed asset is mapped
  from the APK without a copy, a compressed one is inflated into memory
  while it is read. Keep the samples uncompressed; Android does not compress
  `.wav`, `.ogg` and `.mp3` by default, for `.flac` add it to `noCompress`.
  The loads and the streams of notes run on the workers of the engine, as
  with files.
- When the exact name of a sample is not there, the file name is matched
  without regard to case; the folders of the path must match exactly, since
  an APK lists the files of a folder, not its subfolders. No listing file
  is needed.
- The package gets the `AssetManager` itself: its Android plugin hands the
  one of the app to the native library when the process starts, before any
  Dart code runs. An app with its own JNI may call
  `aud_dsp_sampler_set_android_assets` instead. On iOS and the other
  platforms a load of an `apk:/` path ends with `AUD_ERROR_UNSUPPORTED` and
  a message that says so; normal paths work everywhere.
- The store keys a sample by its lexically normal path, so that presets in
  different folders that reach one sample through different relative paths
  share it.

## How It Works

### The node

- `aud.sampler.sfz` has no input and one output bus `out` with 2 to 16
  channels in stereo pairs: sfizz's outputs, one pair each, which the SFZ
  opcode `output` picks per region.
- The event input `midi` takes UMP of MIDI 1.0 and 2.0: notes with their
  velocity, control changes, pitch bend, channel and poly pressure,
  program changes. The other per-note messages of MIDI 2.0 have no
  counterpart in sfizz and count as `AUD_ERROR_UNSUPPORTED`. Numeric
  controls set sfizz's CCs 0 to 511 by their id; messages reach sfizz's
  message API.
- The parameters, in the order of `AudSfzSampler.params`: `volume` (dB,
  -60 to +6, ramped), `polyphony` (1 to 256), `sample_quality` (0 to 10),
  `oscillator_quality` (0 to 3), `tuning_frequency` (400 to 480 Hz) and
  `stretch_tuning` (0 to 1). The stepped ones round to their step. A
  change of the polyphony or the tuning loads the instrument again, and
  the notes that sound fade out.
- The tail of the node is the longest amplitude release of its
  instrument, of the ADSR or of the flex EG that replaces it
  (`egN_ampeg`). The transport reaches sfizz - tempo, time signature,
  position and playing state - for its tempo-synced modulation. An
  offline render runs sfizz at its best quality.

### Loads

- `sfz_file` and `sfz_text` start a load on a worker thread of the engine
  and return its id at once; an empty file or text unloads the
  instrument, as does one without regions, e.g. a text with only a
  comment. `sfz_virtual_path` names the path the samples of the next
  text resolve against; `scala_file` and `scala_text` load a tuning with
  the instrument.
- The load fills a standby synth configured like the one that plays. The
  audio thread adopts it at a block start and fades the old synth out
  over 5 ms; the new synth starts without notes and takes over the
  controllers, the pitch bend, the pressure and the program as restored
  values, so no region that a controller triggers starts.
- A newer load cancels one that runs; a failed load keeps the instrument
  that plays. A load fails when sfizz cannot read the instrument, or
  finds errors and no region in it (the message quotes the first error),
  when it can read none of its samples, or with `AUD_ERROR_CAPACITY` when
  the heads of its samples alone exceed the sample cache budget. A load that
  ends well names the samples it could not read and the effect types sfizz
  does not know, which pass the signal through.
- Before the first instrument the node ramps the volume and remembers the
  controllers for it. No exception crosses the C ABI: a load that runs out
  of memory ends with `AUD_ERROR_OUT_OF_MEMORY`. A host api that lacks a
  function the node calls, or whose engine takes no more caches, gets no
  node.

### The sample store

- One store per process holds the samples of every node of every engine,
  each file once. A load reads the head of every sample, 8192 frames,
  which stays while an instrument uses it; a note streams the whole file
  as a task of the engine.
- When the process holds more than the budget, the engine asks the store
  to trim: first the streamed data no voice reads, then the files no
  instrument holds, the least recently used first. A voice that outruns
  its stream plays silence, keeps its position and counts as
  `AUD_ERROR_UNDERFLOW`.
- A pool without a host - sfizz's C API, the host-less tests of the
  fork - reads whole files at once. A sample with more than 2^31 - 1
  frames, or one that is neither mono nor stereo, is refused and named as
  missing. A sample the memory cannot hold is dropped and named as
  missing; a stream that memory cannot hold leaves its voices on the head.
- SFZ instruments are trusted input: an SFZ file can `#include` or play any
  file the app can read - absolute paths and `../` work, which SFZ
  libraries use to share samples. The sandbox of the app bounds what it
  reaches.
- Besides the filesystem the fork reads files a backend serves, named by a
  scheme: `VirtualFile.h` holds the backends, the parser, the store, the
  audio readers and the Scala tuning ask it first. `apk:/` is the backend
  of the assets of an Android app (`src/aud_apk_assets.cpp`); the native
  tests serve a folder of the host as the assets.

### Messages

- A message below `/graph/<g>/node/<n>/msg/` reaches sfizz's message API:
  `/graph/1/node/2/msg/region0/ampeg_attack f 0.1` becomes sfizz's
  `/region0/ampeg_attack`. sfizz answers on the audio thread through the
  engine, and `AudGraph.messages` delivers the answers.
- A message with a string or a blob argument, or with more than four
  arguments, is refused and counts as `AUD_ERROR_UNSUPPORTED`, since sfizz
  would copy or parse it on the audio thread. The fork drops the messages
  that would grow its regions.

### The fork of sfizz

- `src/third_party/sfizz` started as the subset of `audanika/sfizz` at
  `824afdc` that the linker pulls and is changed in place: the history of
  this package is the history of the fork. Every changed file says what
  changed in a "Modified by Audanika" note below its license header.
  Audanika's code in the fork - those changes and the new
  `RbjFilters.hpp` - is BSD-2-Clause like sfizz, so that the fork has one
  license and its fixes can go upstream.
- The build hook compiles the sources of `SOURCES.txt` with the include
  directories of `INCLUDES.txt`. `NOTICES.md` names the components and
  their licenses, and the result of the Faust audit: the RBJ filters are
  Audanika's own code after the Audio EQ Cookbook, the Faust-generated
  code that stays ships the STK-4.3 notice, and fverb, the metronome and
  cephes are gone.
- The synths of several nodes load on the workers of the engine at the
  same time. What they share in the process - the curves of the flex EG
  shapes, the silent wavetable, the table of the distortion, the CRC
  table of stb_vorbis and the CPU caps of dr_flac and dr_mp3 - is built
  once, atomic or guarded by a mutex that only loads take; `NOTICES.md`
  lists the audit (ticket 28).
- The hot spots of an app's audio thread are faster (ticket 28): the MIDI
  state flushes only the event vectors that received events; the
  compressor calls `log10` only for a level above 0.9999 times its linear
  threshold, 1e-4 below it, at or below which the term of the threshold is
  exactly 0, and `pow` only while its gain is not exactly 0 dB, and passes
  silence through while it is at rest; the gain effect converts its gain
  once, when it is made; a bus whose input is silent skips its effects at
  rest; a voice converts its pitch and its volume modulation once per run
  of equal values. The output stays the same bit for bit, but for the rest
  of the compressor.
- The compressor comes to rest about a second after a loud sound (release
  0.3.2): after a silent block it sets a level below 1e-9, a gain within
  1e-7 dB of 0 dB and a memory of its oversampling filters below 1e-9 to 0.
  An app's limiter rested 4.4 s after a loud sound, and one with a release
  of 0.2 s up to 17.9 s, when its detector fell below the smallest float;
  now after 1.1 s and 4.5 s. A silent block whose filters are silent and
  whose level lies below the threshold only decays the detectors, as their
  loop would. The output moves by at most 1.2e-7, within the 1e-5 of
  decision sampler-007. `NOTICES.md` lists the changed files.
- Opcodes and events of a controller beyond the last one, 511, are
  ignored (release 0.3.2), where they wrote or read behind the controllers
  of the synth: `set_cc512`, `set_hdcc512` and `label_cc512`, the `_onccN`
  opcodes of the delay, the offset, the end and the loop of a sample and of
  the delay and the fade of an LFO, and a CC event.
- The library builds for every ABI of Android, the 32-bit `armeabi-v7a`
  among them, where Abseil's 128-bit integer has no compiler type and
  needs `int128.cc`. On Android it links with `--no-undefined`: a symbol
  the subset lacks fails the build instead of the load in the app.

### The Dart API

- `aud_dsp_sampler.dart` is platform-neutral and compiles for the web:
  `AudSfzSampler` holds the ids of the node, `AudSfzPreset.file` and
  `AudSfzPreset.text` build its presets.
- `aud_dsp_sampler_ffi.dart` adds `AudDspSampler`: `register` is the
  register function for the package list of an engine, `registerWith`
  registers with the host api of a graph and throws an
  `AudDspSamplerException` with the result code when the engine refuses,
  `storeBytes` and `storeFiles` read the store of the process.
- The package is an Android plugin without a Flutter dependency: Flutter
  builds the Java shim in `android/`, a content provider that hands the
  `AssetManager` of the app to the native library when the process starts.
- Every member of `AudDspSampler` is recorded with `@RecordUse`: in a
  profile or release build the link hook keeps the native library only
  when the app uses one of them.
- `testing.dart` adds `AudSfzTestInstrument`: `write` writes saw waves on
  five root keys and their SFZ file, `preset` loads them.

### Tests

```bash
dart test
node scripts/test-native.js
```

- `dart test` runs the Dart tests, the golden renders of the test
  instrument against `test/goldens` and the native tests.
  `UPDATE_GOLDENS=1 dart test` writes the goldens of the test instrument
  anew with the current code.
- `scripts/test-native.js` builds the node, the fork and the engine of
  `aud_audio_graph` with the address and undefined behaviour sanitizers,
  the RealtimeSanitizer - with a probe that proves it catches an
  allocation on the audio thread - and the thread sanitizer, under the
  engine's watchdog. The tests of the fork compile as gnu++14, like sfizz.
- The tests of ticket 28 compare the faster code with the original: the
  MIDI state with the original flush on random event streams, the gain bit
  for bit and the compressor within 1e-6 with their original code on loud,
  quiet, silent, transient and one-sided signals and on sounds after
  pauses, and the render of an instrument like an app's with
  `test/goldens/sfizz_app_instrument.wav`, which release 0.3.0 rendered,
  within the 1e-5 of sampler-007.
- `--bench` builds the micro-benchmark `test/native/bench/sfizz_bench.cpp`
  at -O3 with the fork of release 0.3.0 (`AUD_BENCH_BASE` names another
  git ref) and with the fork of now, prints what the flush of the MIDI
  state, the compressor - also in the two seconds after a loud tone -, the
  gain, an idle synth and four synths cost per call before and after, and
  fails when the renders of both differ by more than 1e-6.
  `UPDATE_GOLDENS=1 node scripts/test-native.js --bench` writes
  `test/goldens/sfizz_app_instrument.wav` with the fork of that release,
  not with the current code; `dart test` never writes it.
- `--help` lists the options, `--only=asan|plain|rtsan|tsan` runs one
  build, and an unknown option ends the run with exit code 2.
  `AUD_TEST_FILTER` runs the tests whose name contains it;
  `AUD_SFIZZ_PRISTINE=<sfizz checkout>` also compares the RBJ filters with
  the Faust originals. The build cache in `.dart_tool` keeps only the
  objects and binaries of the last run.
- On macOS the thread sanitizer names the files and lines of a report
  through LLVM's `llvm-symbolizer` and a dSYM bundle of the test binary
  that LLVM's `dsymutil` writes - from LLVM of Homebrew, or the
  `llvm-symbolizer` that `AUD_LLVM_SYMBOLIZER` names, with `dsymutil`
  beside it; Apple's `atos` hung on reports of races. `AUD_RTSAN_CXX`
  names the compiler of the RealtimeSanitizer build.

## Contributing

Tickets run through the `gg` workflow: see the
[Develop Guide](doc/guides/develop-guide.md) and the
[Review Guide](doc/guides/for-ai/ai-review-guide.md).

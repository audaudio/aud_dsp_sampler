# aud_dsp_sampler

SFZ sampler node of the Audanika Audio Engine on the audaudio fork of
sfizz.

Part of the Audanika Audio Engine; planned in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## The node

`aud.sampler.sfz` plays SFZ instruments on the engine of `aud_audio`:

- One output bus `out`, 2 to 16 channels in stereo pairs.
- An event input `midi`: UMP of MIDI 1.0 and 2.0 - notes with their
  velocity, controllers, pitch bend, channel and poly pressure, program
  changes -, numeric controls as sfizz's CCs by number, and messages to
  sfizz's message API.
- Parameters `volume` (dB, ramped), `polyphony`, `sample_quality`,
  `oscillator_quality`, `tuning_frequency` and `stretch_tuning`. A change
  of the polyphony or the tuning loads the instrument again.
- String settings `sfz_file`, `sfz_virtual_path`, `sfz_text`,
  `scala_file` and `scala_text`. Each one that loads returns the id of its
  load at once; the load runs on a worker thread of the engine.

The node plays the old instrument while a new one loads and swaps them with
a crossfade of 5 ms; the new synth takes over the controllers. A newer load
cancels one that runs; a failed load keeps the instrument that plays. The
samples live in one store per process that every node of every engine
shares, within the engine's sample cache budget. On the audio thread only
sfizz's realtime calls run, without allocating; the native tests run the
node under the engine's watchdog, RealtimeSanitizer and ThreadSanitizer.

```dart
import 'package:aud_audio/aud_audio_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';

final engine = AudEngine(packages: [
  AudNativeNodePackage(AudDspSampler.name, AudDspSampler.register),
]);
engine.prepare();
final piano = engine.graph.createNode(
  AudSfzSampler.typeId,
  preset: AudSfzPreset.file('/path/to/piano.sfz', polyphony: 32),
);
engine.graph.transaction((tx) => tx.connect(piano, engine.graph.io));
engine.start();
```

- `aud_dsp_sampler.dart` (platform-neutral): `AudSfzSampler` with the ids
  of the node, `AudSfzPreset`, which builds node presets.
- `aud_dsp_sampler_ffi.dart`: `AudDspSampler` registers the node and
  reads the store. Its members are recorded with `@RecordUse`: in a
  profile or release build the link hook keeps the native library only
  when an app uses one of them.
- `testing.dart`: `AudSfzTestInstrument` writes an instrument of
  generated samples, for tests, goldens and examples.

## The fork of sfizz

`src/third_party/sfizz` is the audaudio fork of sfizz (decision
sampler-002 of aud_audio_pm): it started as the subset of
`audanika/sfizz` at `824afdc` that the linker pulls and is changed in
place. Every changed file says so in a "Modified by Audanika" note;
`src/third_party/NOTICES.md` lists the components, their licenses and the
license audit of the Faust-generated code. The build hook compiles the
sources of `SOURCES.txt` with the include directories of `INCLUDES.txt`.

## Tests

```bash
dart test
node scripts/test-native.js
```

`dart test` runs the Dart tests, the golden renders of the test instrument
(`test/goldens`, `UPDATE_GOLDENS=1` writes them) and the native tests.
`scripts/test-native.js` builds the node, the fork and the engine of
`aud_audio_graph` with the address and undefined behaviour sanitizers, the
RealtimeSanitizer and the thread sanitizer; `AUD_SFIZZ_PRISTINE=<checkout>`
also compares the RBJ filters with the Faust originals of sfizz.

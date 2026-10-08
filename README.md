# aud_dsp_sampler

SFZ sampler node of the Audanika Audio Engine on the sfizz fork.

Part of the Audanika Audio Engine; planned in [aud_audio_pm](https://github.com/audaudio/aud_audio_pm).

## The spike node (ticket 5)

`aud.sampler.sfz` wraps a sfizz synth as a node of the engine: the
parameter `volume` (dB), note on, note off and control events, and string
settings that load an instrument. sfizz loads only while no realtime call
is in flight, so load first and put the node into the chain afterwards.

```dart
import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';

AudDspSampler.register(engine.hostApi);       // throws AudDspSamplerException
final sampler = engine.createNode(AudSfzSampler.typeId);
engine.setString(sampler, AudSfzSampler.sfzFile, '/path/to/instrument.sfz');
// or: a virtual path that resolves the samples, then SFZ text
engine.setString(sampler, AudSfzSampler.sfzVirtualPath, '/path/to/virtual.sfz');
engine.setString(sampler, AudSfzSampler.sfzText, '<region> sample=a.wav key=60');
engine.setChain([sampler]);
engine.noteOn(sampler, number: 60, velocity: 0.8);
```

## The vendored sfizz

`src/third_party/sfizz` holds the subset of the audanika fork of sfizz
that the node needs — the translation units the linker pulls
(`SOURCES.txt`) and their header closure — with the include directories
in `INCLUDES.txt`; the build hook compiles them together with the node.
Every component's license is listed in `src/third_party/NOTICES.md`.
Refresh the subset by building the fork with its own CMake, linking a
probe against the static libraries with a linker map, and collecting the
header closure with `clang -MM` (the script of ticket 5 lives in the
ticket's notes; it becomes a package script with ticket S9).

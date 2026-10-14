# Third-party notices of aud_dsp_sampler

The directory `sfizz` holds the package's own fork of sfizz (decision
sampler-002): the files the sampler node compiles, listed in
`sfizz/SOURCES.txt`, and the headers they include. It started on 2026-10-08
(ticket 5) as the subset of https://github.com/audanika/sfizz (master,
commit 824afdc, "Update atomic_queue to v1.6.9") that the linker pulls, and
has been changed in place since ticket 25. Every changed file of sfizz says
so in a "Modified by Audanika" note below its license header, and so do the
three decoders of st_audiofile that ticket 28 changed: `dr_flac.h`,
`dr_mp3.h` and `stb_vorbis.c` (see the audit of the shared state below),
and the six oversampling filters of hiir that tell whether they are at rest
(see the hot spots of the audio thread below).
`fs_std_impl.cpp` is the one-line translation unit sfizz's build generates
for the filesystem implementation. Audanika's code in the fork - the
changes the notes name and the new files `sfizz/src/sfizz/RbjFilters.hpp`,
`sfizz/src/sfizz/VirtualFile.h` with `VirtualFile.cpp` (ticket 27:
files a backend serves, e.g. the assets of an APK below `apk:/`) and
`sfizz/src/sfizz/VoiceCount.h` (ticket 29: a count of the voices of a
synth) - is under sfizz's BSD-2-Clause license (decision sampler-002), so
that the fork has one license and its fixes can go upstream.

The Abseil files are those of its release `20240116.0`
(https://github.com/abseil/abseil-cpp, commit 4a2c633, the submodule of
sfizz), unchanged.
`absl/numeric/int128_no_intrinsic.inc` and `absl/numeric/int128.cc` came
with ticket 27: a 32-bit target such as Android's `armeabi-v7a` has no
128-bit integer of the compiler, and Abseil's `uint128` then needs them.

| Component | Path | License |
| --- | --- | --- |
| sfizz | `sfizz/src/sfizz`, `sfizz/src/sfizz.h` | BSD-2-Clause, `sfizz/LICENSE` |
| Audanika's code in the fork | `sfizz/src/sfizz/RbjFilters.hpp`, `sfizz/src/sfizz/VirtualFile.h`, `sfizz/src/sfizz/VirtualFile.cpp`, `sfizz/src/sfizz/VoiceCount.h` and the changes the "Modified by Audanika" notes name | BSD-2-Clause, Copyright (c) 2026 Audanika, header of `RbjFilters.hpp` |
| Faust library functions | `sfizz/src/sfizz/gen/filters`, `sfizz/src/sfizz/effects/gen`, `sfizz/src/sfizz/effects/impl/ResonantString*` | STK-4.3 (MIT-style), `sfizz/LICENSE-STK-4.3.md` |
| Abseil | `sfizz/external/abseil-cpp` | Apache-2.0, `sfizz/external/abseil-cpp/LICENSE` |
| ghc filesystem | `sfizz/external/filesystem` | MIT, `sfizz/external/filesystem/LICENSE` |
| invoke.hpp | `sfizz/external/invoke.hpp` | MIT, `sfizz/external/invoke.hpp/LICENSE.md` |
| jsl | `sfizz/external/jsl` | BSL-1.0, `sfizz/external/jsl/LICENSE.md` |
| SIMDe | `sfizz/external/simde` | MIT, `sfizz/external/simde/COPYING` |
| st_audiofile | `sfizz/external/st_audiofile` | BSD-2-Clause, `sfizz/external/st_audiofile/LICENSE.md` |
| dr_libs | `sfizz/external/st_audiofile/thirdparty/dr_libs` | MIT-0 or public domain, `.../dr_libs/LICENSE` |
| WavPack | `sfizz/external/st_audiofile/thirdparty/wavpack` | BSD-3-Clause, `.../wavpack/COPYING` |
| libaiff | `sfizz/external/st_audiofile/thirdparty/libaiff` | BSD-2-Clause, `.../libaiff/LICENSE` |
| stb_vorbis | `sfizz/external/st_audiofile/thirdparty/stb_vorbis` | MIT or public domain, header of the file |
| KISS FFT | `sfizz/src/external/kiss_fft` | BSD-3-Clause, `sfizz/src/external/kiss_fft/COPYING` |
| pugixml | `sfizz/src/external/pugixml` | MIT, `sfizz/src/external/pugixml/LICENSE.md` |
| spline | `sfizz/src/external/spline` | BSD-3-Clause, `sfizz/src/external/spline/LICENSE` |
| Surge tuning library | `sfizz/src/external/tunings` | MIT, `sfizz/src/external/tunings/LICENSE.md` |
| cpuid | `sfizz/src/external/cpuid` | BSD-3-Clause, `sfizz/src/external/cpuid/LICENSE.rst` |
| hiir | `sfizz/src/external/hiir` | WTFPL, `sfizz/src/external/hiir/license.txt` |

## The license audit of the Faust-generated code

Done for ticket 25 (decisions license-002 and license-003, the topic
`sfizz-faust-audit.md` of aud_audio_pm):

- The RBJ filters and equalizer bands (32 generated headers) were built on
  `rbj.filtercoeff`, which is LGPL with exception. They are gone;
  `RbjFilters.hpp` implements them anew from the Audio EQ Cookbook.
- The fverb effect and the metronome rested on LGPL code too and are gone.
- cephes' `i0` is replaced by a power series of its own in
  `sfizz/src/sfizz/MathHelpers.h`.
- The other generated filters and effects, the strings effect
  (`sfizz/src/sfizz/effects/impl/ResonantString*`) among them, use Faust
  library functions under STK-4.3 (see the notice in
  `sfizz/LICENSE-STK-4.3.md`) and the one-line helpers of the libraries (a
  constant or a sample-rate field, a difference, a conditional, a dB
  conversion or a sample counter), of which no expression survives in the
  generated code.

## The audit of the state the process shares

Done for ticket 28, after an app whose four samplers loaded at the same time
crashed in `FlexEGs::getShapeCurve`: the samplers build their synths on the
workers of the engine side by side (lifecycle-002) while others render, so
what the process holds once, and not each synth, must not be written by one
thread while another reads or writes it. Checked were the writable data of
every object of `sfizz/SOURCES.txt` (`nm`: the sections `__data`, `__bss`
and `__common`, function-local statics included) and the calls of libc
functions that keep global state.

Changed:

- `sfizz/src/sfizz/FlexEGDescription.cpp`: the map of the curves of the flex
  EG shapes (`egN_shapeM`), which every region asks and every load clears,
  takes a mutex. Without it, parallel loads corrupted the map: the crash of
  the app. Only loads take the lock; a voice reads the curve its point
  holds.
- `sfizz/src/sfizz/Wavetables.cpp`: the silent wavetable, which the first
  voice built behind a plain flag, is a thread-safe static; two synths that
  were the first of the process built it into the one table.
- `sfizz/src/sfizz/effects/Disto.cpp`: the table of the Faust distortion
  stages is filled once instead of by every stage that is built or set to a
  sample rate, while the stages of other synths read it. The table does not
  depend on the sample rate, and a new rate still sets the constants of each
  stage, as the original did: the distortion sounds as in 0.3.0 at every
  rate.
- `sfizz/external/st_audiofile/thirdparty/stb_vorbis/stb_vorbis.c`: the CRC
  table, built again on every open of an Ogg file, is built once.
- `sfizz/external/st_audiofile/thirdparty/dr_libs/dr_flac.h`: the CPU caps,
  stored again on every open of a FLAC file, are detected once; the
  attribute that hid this race from the ThreadSanitizer is gone.
- `sfizz/external/st_audiofile/thirdparty/dr_libs/dr_mp3.h`: the detection
  of SSE2, compiled for 32-bit x86 only, stores its result in an atomic
  integer.

Safe already since ticket 25: the tables of the interpolators
(`Interpolators.cpp`) and the SIMD dispatch (`SIMDHelpers.cpp`), set once;
the random generator of `MathHelpers.h`, atomic; the sample store
(`FilePool.cpp`) and the backends of virtual files (`VirtualFile.cpp`),
behind their mutexes.

Safe as they are:

- Constants built before `main`: the default curve (`Curve.cpp`), the pan
  table (`Panning.cpp`), the mipmap tables and harmonic profiles
  (`Wavetables.cpp`), `FileId::emptyFilename`.
- Function-local statics that C++ builds thread-safely and nobody writes
  afterwards: the waves of the oscillators (`Wavetables.cpp`),
  `LFODescription::getDefault`, the S-curve of `Voice.cpp`, the paths `.`
  and `..` of `FilePool.cpp`, the error category of `AudioReader.cpp`, the
  root directory of ghc filesystem.
- The atomic counters of `BufferCounter` (`Buffer.h`).
- Tables nobody writes: the codecs of libaiff, the stream readers of
  WavPack, the Ogg tables of stb_vorbis, the LPC coefficients of dr_flac,
  pugixml's empty node set.
- Hooks nothing sets: Abseil's logging hooks, pugixml's allocation
  functions.
- `ModKeyHash.cpp`'s check of the default hash, compiled into debug builds
  only (the hook builds with `NDEBUG`).
- No compiled file calls a libc function with global state such as
  `strtok`, `rand`, `localtime` or `setlocale`; ghc filesystem calls
  `setlocale` and `getenv` only in functions sfizz does not use.

## The hot spots of the audio thread

Changed for ticket 28, after a profile of an app on an Android tablet whose
four sampler nodes played dense chords: a third of the sampler's time went
to the flush of the MIDI state, and 17 to 25 % of the audio thread to
`log10f`, `powf` and `exp2f` of libm, called for every sample. The output
stays the same, bit for bit; the native tests compare the changed code with
the original on random event streams, on signals for the compressor and on
the render of an app's instrument (`test/native/sfizz_midi_state_test.cpp`,
`sfizz_compressor_test.cpp`, `sfizz_app_render_test.cpp`), and
`node scripts/test-native.js --bench` measures it against release 0.3.0.

- `sfizz/src/sfizz/MidiState.h`, `MidiState.cpp`: `flushEvents()` flushes
  only the event vectors that received an event since the last flush, which
  a list of fixed size names; it flushed all 642 in every block of every
  synth. A vector no event reached holds one event at delay 0, which a flush
  keeps as it is.
- `sfizz/src/sfizz/effects/gen/compressor.hxx`, the Faust code of the
  compressor (STK-4.3 like the rest of the generated code): `log10` runs only
  for a level above a linear level 1e-4 below the threshold, where the
  original's term `max(20 log10(level) - threshold, 0)` is exactly 0 - the
  test checks the 2^22 levels next to it and a sample below -, and `pow`
  only for a gain other than 0 dB, which is 1. The state of the loop stays
  in registers, `computeTwo()` runs the compressors of both channels in one
  loop, and `isAtRest()` tells whether silence passes as silence.
- `sfizz/src/sfizz/effects/Compressor.h`, `Compressor.cpp`: a silent block
  passes as it is while the compressor is at rest, its detectors silent and
  the memory of its oversampling filters silent; the filters and the
  detectors of both channels run in one loop each.
- `sfizz/src/sfizz/effects/Gain.h`, `Gain.cpp`: the gain is converted to
  linear once per block, not once per sample.
- `sfizz/src/sfizz/effects/Nothing.h`, `sfizz/src/sfizz/Effects.h`,
  `Effects.cpp`: an effect tells whether it is at rest; a bus whose input is
  silent and whose effects are all at rest leaves its output silent without
  running them. An effect that does not tell, such as one with a tail that
  rings, runs as before.
- `sfizz/src/sfizz/MathHelpers.h`, `sfizz/src/sfizz/Voice.cpp`: a voice
  converts its pitch to a ratio (`exp2f`) and the gain of a volume
  modulation (`powf`) once per run of equal values, through
  `SameArgumentCache`, instead of once per sample.
- `sfizz/src/external/hiir/hiir/Upsampler2xFpu.h`, `Upsampler2xNeon.h`,
  `Upsampler2xSse.h`, `Downsampler2xFpu.h`, `Downsampler2xNeon.h`,
  `Downsampler2xSse.h`: `is_cleared()` tells whether the memory of the filter
  is silent.

## The voice limit of the host

Changed for ticket 29, so that the sampler node keeps its voices below a
limit of its own and below one voice budget of the process while sfizz
renders: sfizz knows only the polyphony it allocates, and a change of it
loads the instrument again. sfizz's own polyphony checks and their fades of
off_time are unchanged; the goldens of the package render as before.

- `sfizz/src/sfizz/VoiceCount.h` (new): the voices a count finds - those
  that sound, those of them that are released, those that render.
- `sfizz/src/sfizz/Voice.h`, `Voice.cpp`: `steal()` turns a voice off
  without its release; its output fades out linearly over the frames the
  host gives, after its normal rendering, and the voice is cleaned up at
  the end of the fade like one whose envelope ended, without the 50 ms
  fade of the envelope. A stolen voice ignores note-offs and the pedals;
  `isStolen()` tells it.
- `sfizz/src/sfizz/VoiceManager.h`, `VoiceManager.cpp`: `countVoices()`
  counts from the list of active voices instead of walking all voices;
  `stealVoices()` steals sounding voices, released ones before held ones,
  the oldest first, each with its sister voices, sorting in a vector that
  holds as many voices as the synth, so that nothing allocates. A stolen
  voice is no attack voice that lets a release trigger start.
- `sfizz/src/sfizz/Synth.h`, `Synth.cpp`: `countVoices()` and
  `stealVoices()` for the host, through the voice manager.

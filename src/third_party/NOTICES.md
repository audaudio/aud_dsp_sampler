# Third-party notices of aud_dsp_sampler

The directory `sfizz` holds the package's own fork of sfizz (decision
sampler-002): the files the sampler node compiles, listed in
`sfizz/SOURCES.txt`, and the headers they include. It started on 2026-10-08
(ticket 5) as the subset of https://github.com/audanika/sfizz (master,
commit 824afdc, "Update atomic_queue to v1.6.9") that the linker pulls, and
has been changed in place since ticket 25. Every changed file of sfizz says
so in a "Modified by Audanika" note below its license header.
`fs_std_impl.cpp` is the one-line translation unit sfizz's build generates
for the filesystem implementation. `sfizz/src/sfizz/RbjFilters.hpp` is
Audanika's own code under the license of this package.

| Component | Path | License |
| --- | --- | --- |
| sfizz | `sfizz/src/sfizz`, `sfizz/src/sfizz.h` | BSD-2-Clause, `sfizz/LICENSE` |
| Faust library functions | `sfizz/src/sfizz/gen/filters`, `sfizz/src/sfizz/effects/gen` | STK, `sfizz/LICENSE-STK-4.3.md` |
| Abseil | `sfizz/external/abseil-cpp` | Apache-2.0, `sfizz/external/abseil-cpp/LICENSE` |
| atomic_queue | `sfizz/external/atomic_queue` | MIT, `sfizz/external/atomic_queue/LICENSE` |
| ghc filesystem | `sfizz/external/filesystem` | MIT, `sfizz/external/filesystem/LICENSE` |
| invoke.hpp | `sfizz/external/invoke.hpp` | MIT, `sfizz/external/invoke.hpp/LICENSE.md` |
| jsl | `sfizz/external/jsl` | BSL-1.0, `sfizz/external/jsl/LICENSE.md` |
| SIMDe | `sfizz/external/simde` | MIT, `sfizz/external/simde/COPYING` |
| st_audiofile | `sfizz/external/st_audiofile` | BSD-2-Clause, `sfizz/external/st_audiofile/LICENSE.md` |
| dr_libs | `sfizz/external/st_audiofile/thirdparty/dr_libs` | MIT-0 or public domain, `.../dr_libs/LICENSE` |
| WavPack | `sfizz/external/st_audiofile/thirdparty/wavpack` | BSD-3-Clause, `.../wavpack/COPYING` |
| libaiff | `sfizz/external/st_audiofile/thirdparty/libaiff` | BSD-2-Clause, `.../libaiff/LICENSE` |
| stb_vorbis | `sfizz/external/st_audiofile/thirdparty/stb_vorbis` | MIT or public domain, header of the file |
| threadpool | `sfizz/external/threadpool` | zlib, header of the file |
| KISS FFT | `sfizz/src/external/kiss_fft` | BSD-3-Clause, `sfizz/src/external/kiss_fft/COPYING` |
| pugixml | `sfizz/src/external/pugixml` | MIT, `sfizz/src/external/pugixml/LICENSE.md` |
| spline | `sfizz/src/external/spline` | BSD-3-Clause, `sfizz/src/external/spline/LICENSE` |
| Surge tuning library | `sfizz/src/external/tunings` | MIT, `sfizz/src/external/tunings/LICENSE.md` |
| cpuid | `sfizz/src/external/cpuid` | BSD-3-Clause, `sfizz/src/external/cpuid/LICENSE.rst` |
| hiir | `sfizz/src/external/hiir` | WTFPL, `sfizz/src/external/hiir/license.txt` |

## The license audit of the Faust-generated code

Done for ticket 25 (decision license-002, the topic
`sfizz-faust-audit.md` of aud_audio_pm):

- The RBJ filters and equalizer bands (32 generated headers) were built on
  `rbj.filtercoeff`, which is LGPL with exception. They are gone;
  `RbjFilters.hpp` implements them anew from the Audio EQ Cookbook.
- The fverb effect and the metronome rested on LGPL code too and are gone.
- cephes' `i0` is replaced by a power series of its own in
  `sfizz/src/sfizz/MathHelpers.h`.
- The other generated filters and effects use Faust library functions
  under STK-4.3 (see the notice in `sfizz/LICENSE-STK-4.3.md`) and the
  one-line helpers of the libraries (a constant, a conversion, a sample
  counter), of which no expression survives in the generated code.

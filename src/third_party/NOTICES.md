# Third-party notices of aud_dsp_sampler

The directory `sfizz` holds the subset of the audanika fork of sfizz that the
sampler node compiles: the files the linker pulls into the library (listed
in `sfizz/SOURCES.txt`) and the headers they include. Source:
https://github.com/audanika/sfizz (master, commit 824afdc, "Update
atomic_queue to v1.6.9"), vendored on 2026-10-08 for ticket 5. `fs_std_impl.cpp`
is the one-line translation unit sfizz's build generates for the
filesystem implementation.

| Component | Path | License |
| --- | --- | --- |
| sfizz | `sfizz/src/sfizz`, `sfizz/src/sfizz.h` | BSD-2-Clause, `sfizz/LICENSE` |
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
| cephes | `sfizz/external/cephes` | BSD-3-Clause, `sfizz/external/cephes/LICENSE.txt` |
| threadpool | `sfizz/external/threadpool` | zlib, header of the file |
| KISS FFT | `sfizz/src/external/kiss_fft` | BSD-3-Clause, `sfizz/src/external/kiss_fft/COPYING` |
| pugixml | `sfizz/src/external/pugixml` | MIT, `sfizz/src/external/pugixml/LICENSE.md` |
| spline | `sfizz/src/external/spline` | BSD-3-Clause, `sfizz/src/external/spline/LICENSE` |
| Surge tuning library | `sfizz/src/external/tunings` | MIT, `sfizz/src/external/tunings/LICENSE.md` |
| cpuid | `sfizz/src/external/cpuid` | BSD-3-Clause, `sfizz/src/external/cpuid/LICENSE.rst` |
| hiir | `sfizz/src/external/hiir` | WTFPL, `sfizz/src/external/hiir/license.txt` |

Open license work (decision sampler-001, license-002): the Faust-generated
effect modules under `sfizz/src/sfizz/effects/gen` and `sfizz/src/sfizz/gen`
must be audited for STK-4.3 or MIT provenance before the first release of
this package; modules from other sources are dropped or re-implemented.

// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

#include "Interpolators.h"

namespace sfz {

// Modified by Audanika for aud_dsp_sampler (ticket 25 of aud_audio_pm): the
// tables are set once per process. Every synth asks for them, also on a
// worker thread while other synths render and read them.
void initializeInterpolators()
{
    static const bool initialized = [] {
        SincInterpolatorTraits<8>::initialize();
        SincInterpolatorTraits<12>::initialize();
        SincInterpolatorTraits<16>::initialize();
        SincInterpolatorTraits<24>::initialize();
        SincInterpolatorTraits<36>::initialize();
        SincInterpolatorTraits<48>::initialize();
        SincInterpolatorTraits<60>::initialize();
        SincInterpolatorTraits<72>::initialize();
        return true;
    }();
    (void)initialized;
}

} // namespace sfz

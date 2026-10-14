// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 27 of aud_audio_pm): a
// block shorter than the block size of the synth is processed without a
// failed size check, which printed to stderr on the audio thread.
// Ticket 28: the gain, which holds for the block, is converted to linear
// once per block instead of once per sample: the same factor, without a
// pow per sample on the audio thread. A finite gain is at rest.

/**
   Note(jpc): this effect is book-only, mentioned but not documented

   Note(jpc): implementation status

- [x] gain
- [ ] gain_oncc
 */

#include "Gain.h"
#include "Opcode.h"
#include "SIMDHelpers.h"
#include "absl/memory/memory.h"
#include <cmath>

namespace sfz {
namespace fx {

    void Gain::setSampleRate(double sampleRate)
    {
        (void)sampleRate;
    }

    void Gain::setSamplesPerBlock(int samplesPerBlock)
    {
        (void)samplesPerBlock;
    }

    void Gain::clear()
    {
    }

    void Gain::process(const float* const inputs[], float* const outputs[], unsigned nframes)
    {
        // to linear
        const float gain = linearGain();

        for (unsigned c = 0; c < EffectChannels; ++c) {
            absl::Span<const float> input { inputs[c], nframes };
            absl::Span<float> output { outputs[c], nframes };
            sfz::applyGain1(gain, input, output);
        }
    }

    float Gain::linearGain() const
    {
        return std::pow(10.0f, 0.05f * _gain);
    }

    bool Gain::isAtRest() const
    {
        // A silent input times a finite gain is silent.
        return std::isfinite(linearGain());
    }

    std::unique_ptr<Effect> Gain::makeInstance(absl::Span<const Opcode> members)
    {
        Gain* gain = new Gain;
        std::unique_ptr<Effect> fx { gain };

        for (const Opcode& opc : members) {
            switch (opc.lettersOnlyHash) {
            case hash("gain"):
                gain->_gain = opc.read(Default::volume);
                break;
            }
        }

        return fx;
    }

} // namespace fx
} // namespace sfz

// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 28 of aud_audio_pm): the
// gain is converted to linear once per block and needs no buffer; a finite
// gain is at rest. Release 0.3.2: once, when the effect is made.

#pragma once
#include "Effects.h"

namespace sfz {
namespace fx {

    /**
     * @brief Gain effect
     */
    class Gain : public Effect {
    public:
        /**
         * @brief Initializes with the given sample rate.
         */
        void setSampleRate(double sampleRate) override;

        /**
         * @brief Sets the maximum number of frames to render at a time. The actual
         * value can be lower but should never be higher.
         */
        void setSamplesPerBlock(int samplesPerBlock) override;

        /**
         * @brief Reset the state to initial.
         */
        void clear() override;

        /**
         * @brief Copy the input signal to the output
         */
        void process(const float* const inputs[], float* const outputs[], unsigned nframes) override;

        /**
          * @brief Whether the gain is finite: silence then stays silent
          * (Audanika, ticket 28).
          */
        bool isAtRest() const override;

        /**
          * @brief The linear factor of the gain, which makeInstance converts
          * once (Audanika, ticket 28).
          */
        float linearGain() const noexcept { return _linearGain; }

        /**
          * @brief Instantiates given the contents of the <effect> block.
          */
        static std::unique_ptr<Effect> makeInstance(absl::Span<const Opcode> members);

    private:
        float _gain = 0; // in dB
        float _linearGain = 1; // 10^(_gain / 20), converted in makeInstance
    };

} // namespace fx
} // namespace sfz

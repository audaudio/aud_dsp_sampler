// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 28 of aud_audio_pm): the
// effect, which copies its input, is always at rest.

#pragma once
#include "Effects.h"

namespace sfz {
namespace fx {

    /**
     * @brief Effect which does nothing
     */
    class Nothing : public Effect {
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
         * @brief Always: silence passes as silence (Audanika, ticket 28).
         */
        bool isAtRest() const override { return true; }
    };

} // namespace fx
} // namespace sfz

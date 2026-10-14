// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2026, Audanika
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// A count of the voices of a synth (aud_dsp_sampler, ticket 29 of
// aud_audio_pm): what Synth::countVoices() finds and what
// Synth::stealVoices() takes, so that a host can keep the voices that sound
// below a limit of its own while the synth renders.

#pragma once

namespace sfz {

/**
 * @brief A count of voices.
 */
struct VoiceCount {
    /**
     * @brief The voices that sound: they play and are neither turned off
     * nor stolen.
     */
    int sounding { 0 };
    /**
     * @brief Of the sounding voices, those that are released: their key and
     * their pedals are up, they play their release.
     */
    int released { 0 };
    /**
     * @brief The voices the next block renders: every voice that is not
     * free, the ones that fade out included.
     */
    int rendered { 0 };
};

} // namespace sfz

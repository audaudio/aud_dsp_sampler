// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 27 of aud_audio_pm):
// setReleaseTime() lets a voice that is turned off, e.g. stolen, fade
// quickly instead of over the stages after its sustain point.

#pragma once
#include <absl/types/span.h>
#include <memory>

namespace sfz {
struct FlexEGDescription;
class Resources;

/**
   Flex envelope generator (according to ARIA)
 */
class FlexEnvelope {
public:
    explicit FlexEnvelope(Resources &resources);
    ~FlexEnvelope();

    /**
       Sets the sample rate.
     */
    void setSampleRate(double sampleRate);

    /**
       Attach some control parameters to this EG.
       The control structure is owned by the caller.
     */
    void configure(const FlexEGDescription* desc);

    /**
     * @brief Set the EG to be freeRunning or not
     *
     * @param freeRunning
     */
    void setFreeRunning(bool freeRunning);

    /**
       Start processing an EG as a region is triggered.
     */
    void start(unsigned triggerDelay);

    /**
       Release the EG.
     */
    void release(unsigned releaseDelay);

    /**
       Cancel the release
     */
    void cancelRelease(unsigned delay);

    /**
       Replace the release by a linear fade from the level at the release
       to zero over `timeInSeconds`, also when the EG is releasing already:
       a voice that is turned off fades like this. The release can no longer
       be cancelled then. (aud_dsp_sampler, ticket 27)
     */
    void setReleaseTime(float timeInSeconds);

    /**
       Get the remaining delay samples
     */
    unsigned getRemainingDelay() const noexcept;

    /**
       Is the envelope released?
    */
    bool isReleased() const noexcept;

    /**
       Is the envelope finished?
    */
    bool isFinished() const noexcept;

    /**
       Process a cycle of the generator.
     */
    void process(absl::Span<float> out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sfz

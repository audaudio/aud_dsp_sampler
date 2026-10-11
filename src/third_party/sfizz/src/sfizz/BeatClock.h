// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 25 of aud_audio_pm): the
// beat numbers, their quantization and the beats per bar of every frame are
// gone with the metronome, their only reader.

#pragma once
#include "Buffer.h"
#include <absl/types/span.h>
#include <vector>
#include <iosfwd>

namespace sfz {

/**
 * @brief Musical time signature
 */
struct TimeSignature {
    TimeSignature() {}
    TimeSignature(int beatsPerBar, int beatUnit) : beatsPerBar(beatsPerBar), beatUnit(beatUnit) {}

    /**
     * @brief Check the signature validity.
     * Valid signatures have a strictly positive numerator and denominator.
     */
    bool valid() const { return beatsPerBar > 0 && beatUnit > 0; }

    bool operator==(const TimeSignature& other) const;
    bool operator!=(const TimeSignature& other) const;

    /**
     * @brief Time signature numerator, indicating the number of beats in a bar
     */
    int beatsPerBar = 0;
    /**
     * @brief Time signature denominator, indicating the type of note (4=quarter)
     */
    int beatUnit = 0;
};

/**
 * @brief Musical time in BBT form
 */
struct BBT {
    BBT() {}
    BBT(int bar, double beat) : bar(bar), beat(beat) {}

    /**
     * @brief Convert the time to a different signature.
     */
    BBT toSignature(TimeSignature oldSig, TimeSignature newSig) const;
    /**
     * @brief Convert the time to a fractional quantity in beats.
     */
    double toBeats(TimeSignature sig) const;
    /**
     * @brief Convert the time to a fractional quantity in bars.
     */
    double toBars(TimeSignature sig) const;
    /**
     * @brief Convert the fractional quantity in beats to musical time.
     */
    static BBT fromBeats(TimeSignature sig, double beats);

    /**
     * @brief Bar number
     */
    int bar = 0;
    /**
     * @brief Beat and tick, stored in the integral and fractional parts
     */
    double beat = 0;
};

class BeatClock {
public:
    /**
     * @brief Set the sample rate.
     */
    void setSampleRate(double sampleRate);
    /**
     * @brief Set the block size.
     */
    void setSamplesPerBlock(unsigned samplesPerBlock);
    /**
     * @brief Reinitialize the current state.
     */
    void clear();
    /**
     * @brief Start a new cycle of clock processing.
     */
    void beginCycle(unsigned numFrames);
    /**
     * @brief End the current cycle of clock processing.
     */
    void endCycle();
    /**
     * @brief Set the tempo.
     */
    void setTempo(unsigned delay, double secondsPerBeat);
    /**
     * @brief Set the time signature.
     */
    void setTimeSignature(unsigned delay, TimeSignature newSig);
    /**
     * @brief Get the time signature
     *
     */
    TimeSignature getTimeSignature() const noexcept { return timeSig_; }
    /**
     * @brief Set the time position.
     */
    void setTimePosition(unsigned delay, BBT newPos);
    /**
     * @brief Set whether the clock is ticking or stopped.
     */
    void setPlaying(unsigned delay, bool playing);
    /**
     * Check whether the clock is currently ticking.
     */
    bool isPlaying() const noexcept { return isPlaying_; }
    /**
     * @brief Get the beat position for each frame of the current cycle.
     *
     * The beat boundaries can be traversed erratically due to approximation
     * errors.
     */
    absl::Span<const float> getRunningBeatPosition();

    /**
     * @brief Get the last BeatPosition
     *
     * @return float
     */
    double getLastBeatPosition() const;
    /**
     * @brief Get the Beats Per Frame object
     *
     * @return float
     */
    double getBeatsPerFrame() const { return beatsPerSecond_ * samplePeriod_; }
    /**
     * @brief Get beats per second
     *
     * @return float
     */
    double getBeatsPerSecond() const { return beatsPerSecond_; }
    /**
     * @brief Create a normalized phase signal for LFO which completes a
     *        period every N-th beat.
     */
    void calculatePhase(float beatPeriod, float* phaseOut);
    /**
     * @brief Create a normalized phase signal for LFO which completes a
     *        period every N-th beat, where N can vary over time.
     */
    void calculatePhaseModulated(const float* beatPeriodData, float* phaseOut);

private:
    void fillBufferUpTo(unsigned delay);

private:
    double samplePeriod_ { 1.0 / config::defaultSampleRate };

    // status of current cycle
    unsigned currentCycleFrames_ = 0;
    unsigned currentCycleFill_ = 0;
    BBT currentCycleStartPos_;

    // musical time information from host
    double beatsPerSecond_ = 2.0;
    TimeSignature timeSig_ { 4, 4 };
    bool isPlaying_ = false;

    // last time position received from host
    BBT lastHostPos_;
    bool mustApplyHostPos_ = false;

    // plugin-side counter
    BBT lastClientPos_;

    Buffer<float> runningBeatPosition_ { config::defaultSamplesPerBlock };
};

} // namespace sfz

std::ostream& operator<<(std::ostream& os, const sfz::BBT& pos);
std::ostream& operator<<(std::ostream& os, const sfz::TimeSignature& sig);

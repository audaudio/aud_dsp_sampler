// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 25 of aud_audio_pm): the
// beat numbers, their quantization and the beats per bar of every frame are
// gone with the metronome, their only reader.

#include "BeatClock.h"
#include "SIMDHelpers.h"
#include "Config.h"
#include "utility/Debug.h"
#include <iostream>
#include <cmath>

namespace sfz {

bool TimeSignature::operator==(const TimeSignature& other) const
{
    return beatsPerBar == other.beatsPerBar && beatUnit == other.beatUnit;
}

bool TimeSignature::operator!=(const TimeSignature& other) const
{
    return !operator==(other);
}

///
BBT BBT::toSignature(TimeSignature oldSig, TimeSignature newSig) const
{
    double beatsInOldSig = toBeats(oldSig);
    double beatsInNewSig = beatsInOldSig * newSig.beatUnit / oldSig.beatUnit;
    return BBT::fromBeats(newSig, beatsInNewSig);
}

double BBT::toBeats(TimeSignature sig) const
{
    return beat + bar * sig.beatsPerBar;
}

BBT BBT::fromBeats(TimeSignature sig, double beats)
{
    int newBar = static_cast<int>(beats / sig.beatsPerBar);
    double newBeat = beats - newBar * sig.beatsPerBar;
    return BBT(newBar, newBeat);
}

double BBT::toBars(TimeSignature sig) const
{
    return bar + beat / sig.beatsPerBar;
}

///
void BeatClock::clear()
{
    beatsPerSecond_ = 2.0;
    timeSig_ =  { 4, 4 };
    isPlaying_ = false;

    lastHostPos_ = { 0, 0 };
    lastClientPos_ = { 0, 0 };
}

void BeatClock::beginCycle(unsigned numFrames)
{
    currentCycleFrames_ = numFrames;
    currentCycleFill_ = 0;
    currentCycleStartPos_ = lastClientPos_;
}

void BeatClock::endCycle()
{
    fillBufferUpTo(currentCycleFrames_);
}

void BeatClock::setSampleRate(double sampleRate)
{
    samplePeriod_ = 1.0 / sampleRate;
}

void BeatClock::setSamplesPerBlock(unsigned samplesPerBlock)
{
    runningBeatPosition_.resize(samplesPerBlock);
}

void BeatClock::setTempo(unsigned delay, double secondsPerBeat)
{
    fillBufferUpTo(delay);

    beatsPerSecond_ = 1.0 / secondsPerBeat;
}

void BeatClock::setTimeSignature(unsigned delay, TimeSignature newSig)
{
    fillBufferUpTo(delay);

    if (!newSig.valid()) {
        CHECKFALSE;
        return;
    }

    TimeSignature oldSig = timeSig_;
    if (oldSig == newSig)
        return;

    timeSig_ = newSig;

    // convert time to new signature
    lastHostPos_ = lastHostPos_.toSignature(oldSig, newSig);
    lastClientPos_ = lastClientPos_.toSignature(oldSig, newSig);
}

void BeatClock::setTimePosition(unsigned delay, BBT newPos)
{
    fillBufferUpTo(delay);

    lastHostPos_ = newPos;

    // apply host position in the next frame
    mustApplyHostPos_ = true;
}

void BeatClock::setPlaying(unsigned delay, bool playing)
{
    fillBufferUpTo(delay);

    isPlaying_ = playing;
}

absl::Span<const float> BeatClock::getRunningBeatPosition()
{
    fillBufferUpTo(currentCycleFrames_);

    return absl::MakeConstSpan(runningBeatPosition_.data(), currentCycleFrames_);
}

double BeatClock::getLastBeatPosition() const
{
    return lastClientPos_.toBeats(timeSig_);
}

void BeatClock::fillBufferUpTo(unsigned delay)
{
    float *beatNumberPosition = runningBeatPosition_.data();
    unsigned fillIdx = currentCycleFill_;

    const TimeSignature sig = timeSig_;

    BBT clientPos = lastClientPos_;
    const BBT hostPos = lastHostPos_;
    bool mustApplyHostPos = mustApplyHostPos_;

    if (!isPlaying_) {
        clientPos = mustApplyHostPos ? hostPos : clientPos;
        mustApplyHostPos = false;

        if (fillIdx < delay) {
            double beats = clientPos.toBeats(sig);
            fill(absl::MakeSpan(&beatNumberPosition[fillIdx], delay - fillIdx), static_cast<float>(beats));
            fillIdx = delay;
        }
    } else {
        for (; fillIdx < delay; ++fillIdx) {
            clientPos = BBT::fromBeats(sig, clientPos.toBeats(sig) + getBeatsPerFrame());
            clientPos = mustApplyHostPos ? hostPos : clientPos;
            mustApplyHostPos = false;

            double beats = clientPos.toBeats(sig);
            beatNumberPosition[fillIdx] = static_cast<float>(beats);
        }
    }


    currentCycleFill_ = fillIdx;
    lastClientPos_ = clientPos;
    mustApplyHostPos_ = mustApplyHostPos;
}

void BeatClock::calculatePhase(float beatPeriod, float* phaseOut)
{
    const unsigned numFrames = currentCycleFrames_;

    if (beatPeriod <= 0.0f) {
        fill(absl::MakeSpan(phaseOut, numFrames), 0.0f);
        return;
    }

    const float invBeatPeriod = 1.0f / beatPeriod;
    const float* beatPositionData = getRunningBeatPosition().data();

    for (unsigned i = 0; i < numFrames; ++i) {
        float beatPosition = std::max(0.0f, beatPositionData[i]);
        float phase = beatPosition * invBeatPeriod;
        phase -= static_cast<int>(phase);
        phaseOut[i] = phase;
    }
}

void BeatClock::calculatePhaseModulated(const float* beatPeriodData, float* phaseOut)
{
    const unsigned numFrames = currentCycleFrames_;
    const float* beatPositionData = getRunningBeatPosition().data();

    for (unsigned i = 0; i < numFrames; ++i) {
        float beatPeriod = beatPeriodData[i];
        float beatPosition = std::max(0.0f, beatPositionData[i]);
        float phase = beatPosition / beatPeriod;
        phase -= static_cast<int>(phase);
        phaseOut[i] = (beatPeriod > 0.0f) ? phase : 0.0f;
    }
}

} // namespace sfz

std::ostream& operator<<(std::ostream& os, const sfz::BBT& pos)
{
    return os << pos.bar << ':' << std::fixed << pos.beat;
}

std::ostream& operator<<(std::ostream& os, const sfz::TimeSignature& sig)
{
    return os << sig.beatsPerBar << '/' << sig.beatUnit;
}

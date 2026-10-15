// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 27 of aud_audio_pm): a
// voice that is turned off, e.g. stolen, fades linearly from where it is
// over a release time of its own (setReleaseTime) instead of running through
// the stages after its sustain point; that release cannot be cancelled.
// Ticket 30: a stage renders as a whole instead of frame by frame: a
// sustain past the time of its stage fills its level, the frames of other
// stages sum their times first (stepSum()) and then take their levels, on
// arm64 four frames at once (stageLevels()), with the same results.

/*
   Note(jpc): implementation status

- [ ] egN_points (purpose unknown)
- [x] egN_timeX
- [x] egN_timeX_onccY
- [x] egN_levelX
- [x] egN_levelX_onccY
- [x] egN_shapeX
- [ ] egN_shapeX_onccY
- [x] egN_sustain
- [ ] egN_dynamic
- [ ] egN_loop
- [ ] egN_loop_shape
- [ ] egN_loop_count
*/

#include "FlexEnvelope.h"
#include "FlexEGDescription.h"
#include "Curve.h"
#include "MidiState.h"
#include "Resources.h"
#include "Config.h"
#include "SIMDHelpers.h"
#include <absl/types/optional.h>
#include <algorithm>

#if SFIZZ_HAVE_NEON && SFIZZ_CPU_FAMILY_AARCH64
#include <arm_neon.h>
#define SFIZZ_AUD_LEVELS_NEON 1
#else
#define SFIZZ_AUD_LEVELS_NEON 0
#endif

namespace sfz {

namespace {

// The levels of a stage at the times of `frames` frames, in place: what
// Curve::evalNormalized() and the sum with the source level give for each
// time, four frames at once on arm64 (aud_dsp_sampler, ticket 30). The
// curve interpolates between two of its points; in a stage longer than a
// few blocks four frames mostly share them, and take them from one load.
// The fused multiply-adds are those the compiler makes of the scalar code.
void stageLevels(const Curve& curve, float inverseTime, float sourceLevel, float targetLevel,
    float* frames, size_t size) noexcept
{
    size_t i = 0;
#if SFIZZ_AUD_LEVELS_NEON
    const float* points = curve.points();
    const float32x4_t inverse = vdupq_n_f32(inverseTime);
    const float32x4_t source = vdupq_n_f32(sourceLevel);
    const float32x4_t range = vdupq_n_f32(targetLevel - sourceLevel);
    for (; i + 4 <= size; i += 4) {
        const float32x4_t x = vmulq_f32(vld1q_f32(frames + i), inverse);
        float32x4_t value7 = vmulq_f32(x, vdupq_n_f32(127.0f));
        value7 = vmaxq_f32(vminq_f32(value7, vdupq_n_f32(127.0f)), vdupq_n_f32(0.0f));
        const int32x4_t i1 = vcvtq_s32_f32(value7);
        const float32x4_t mu = vsubq_f32(value7, vcvtq_f32_s32(i1));
        const int32_t lowest = vminvq_s32(i1);
        float32x4_t below;
        float32x4_t above;
        if (lowest == vmaxvq_s32(i1)) {
            below = vdupq_n_f32(points[lowest]);
            above = vdupq_n_f32(points[std::min(127, lowest + 1)]);
        } else {
            int32_t index[4];
            vst1q_s32(index, i1);
            const float lows[4] = { points[index[0]], points[index[1]], points[index[2]], points[index[3]] };
            const float highs[4] = { points[std::min(127, index[0] + 1)], points[std::min(127, index[1] + 1)],
                points[std::min(127, index[2] + 1)], points[std::min(127, index[3] + 1)] };
            below = vld1q_f32(lows);
            above = vld1q_f32(highs);
        }
        const float32x4_t c = vfmaq_f32(below, mu, vsubq_f32(above, below));
        vst1q_f32(frames + i, vfmaq_f32(source, c, range));
    }
#endif
    for (; i < size; ++i) {
        float x = frames[i] * inverseTime;
        float c = curve.evalNormalized(x);
        frames[i] = sourceLevel + c * (targetLevel - sourceLevel);
    }
}

} // namespace

struct FlexEnvelope::Impl {
    const Resources* resources_ { nullptr };
    const FlexEGDescription* desc_ { nullptr };
    float samplePeriod_ { 1.0 / config::defaultSampleRate };
    size_t delayFramesLeft_ { 0 };

    //
    float stageSourceLevel_ { 0.0 };
    float stageTargetLevel_ { 0.0 };
    float stageTime_ { 0.0 };
    bool stageSustained_ { false };
    const Curve* stageCurve_ { nullptr };

    //
    unsigned currentStageNumber_ { 0 };
    float currentLevel_ { 0.0 };
    float currentTime_ { 0.0 };
    absl::optional<size_t> currentFramesUntilRelease_ { absl::nullopt };
    bool isReleased_ { false };
    bool freeRunning_ { false };
    // The time of the fade of a voice turned off, below 0 without one, and
    // whether the fade runs (aud_dsp_sampler, ticket 27).
    float offTime_ { -1.0f };
    bool offStage_ { false };

    //
    void process(absl::Span<float> out);
    bool advanceToStage(unsigned stageNumber);
    bool advanceToNextStage();
    void updateCurrentTimeAndLevel(int delay = 0);
    void enterOffStage();
};

FlexEnvelope::FlexEnvelope(Resources &resources)
    : impl_(new Impl)
{
    Impl& impl = *impl_;
    impl.resources_ = &resources;
}

FlexEnvelope::~FlexEnvelope()
{
}

void FlexEnvelope::setSampleRate(double sampleRate)
{
    Impl& impl = *impl_;
    impl.samplePeriod_ = 1.0 / sampleRate;
}

void FlexEnvelope::configure(const FlexEGDescription* desc)
{
    Impl& impl = *impl_;
    impl.desc_ = desc;

    //
    impl.freeRunning_ = false;
    impl.isReleased_ = false;
    impl.offTime_ = -1.0f;
    impl.offStage_ = false;

    //
    impl.currentStageNumber_ = 0;
    impl.currentLevel_ = 0.0;
    impl.currentTime_ = 0.0;
}

void FlexEnvelope::start(unsigned triggerDelay)
{
    Impl& impl = *impl_;
    impl.delayFramesLeft_ = triggerDelay;
    impl.currentFramesUntilRelease_ = absl::nullopt;
    impl.advanceToStage(0);
}

void FlexEnvelope::setFreeRunning(bool freeRunning)
{
    Impl& impl = *impl_;
    impl.freeRunning_ = freeRunning;
}

void FlexEnvelope::release(unsigned releaseDelay)
{
    Impl& impl = *impl_;
    impl.currentFramesUntilRelease_ = releaseDelay;
}

void FlexEnvelope::cancelRelease(unsigned delay)
{
    UNUSED(delay);
    Impl& impl = *impl_;
    const FlexEGDescription& desc = *impl.desc_;

    // A voice turned off stays off.
    if (impl.offTime_ >= 0.0f)
        return;

    if (impl.currentFramesUntilRelease_) {
        // Cancel the future release
        impl.currentFramesUntilRelease_ = absl::nullopt;
        return;
    }

    if (!impl.isReleased_)
        return;

    impl.isReleased_ = false;
    impl.advanceToStage(desc.sustain);
    impl.stageTargetLevel_ = impl.currentLevel_;
}

void FlexEnvelope::setReleaseTime(float timeInSeconds)
{
    Impl& impl = *impl_;
    impl.offTime_ = std::max(0.0f, timeInSeconds);
    // A fade that runs already starts again from where it is.
    impl.offStage_ = false;
}

unsigned FlexEnvelope::getRemainingDelay() const noexcept
{
    const Impl& impl = *impl_;
    return static_cast<unsigned>(impl.delayFramesLeft_);
}

bool FlexEnvelope::isReleased() const noexcept
{
    const Impl& impl = *impl_;
    return impl.isReleased_;
}

bool FlexEnvelope::isFinished() const noexcept
{
    const Impl& impl = *impl_;
    const FlexEGDescription& desc = *impl.desc_;
    return impl.currentStageNumber_ >= desc.points.size();
}

void FlexEnvelope::process(absl::Span<float> out)
{
    Impl& impl = *impl_;
    if (impl.desc_->dynamic) {
        int processed = 0;
        int remaining = static_cast<int>(out.size());
        while(remaining > 0) {
            impl.updateCurrentTimeAndLevel(processed);
            int chunkSize = min(config::processChunkSize, remaining);
            impl.process(out.subspan(processed, chunkSize));
            processed += chunkSize;
            remaining -= chunkSize;
        }
    } else {
        impl.process(out);
    }
}

void FlexEnvelope::Impl::process(absl::Span<float> out)
{
    const FlexEGDescription& desc = *desc_;
    size_t numFrames = out.size();
    const float samplePeriod = samplePeriod_;

    // Skip the initial delay, for frame-accurate trigger
    size_t skipFrames = std::min(numFrames, delayFramesLeft_);
    if (skipFrames > 0) {
        delayFramesLeft_ -= skipFrames;
        fill(absl::MakeSpan(out.data(), skipFrames), 0.0f);
        out.remove_prefix(skipFrames);
        numFrames -= skipFrames;
    }

    // Envelope finished?
    if (currentStageNumber_ >= desc.points.size()) {
        fill(out, 0.0f);
        return;
    }

    size_t frameIndex = 0;

    while (frameIndex < numFrames) {
        // Check for release
        if (currentFramesUntilRelease_ && *currentFramesUntilRelease_ == 0) {
            isReleased_ = true;
            currentFramesUntilRelease_ = absl::nullopt;
        }

        // Perform stage transitions
        if (isReleased_ && offTime_ >= 0.0f) {
            // a voice turned off fades from where it is
            if (!offStage_)
                enterOffStage();
        }
        else if (isReleased_) {
            // on release, fast forward past the sustain stage
            const unsigned sustainStage = desc.sustain;
            while (currentStageNumber_ <= sustainStage) {
                if (!advanceToNextStage()) {
                    out.remove_prefix(frameIndex);
                    fill(out, 0.0f);
                    return;
                }
            }
        }
        while ((!stageSustained_ || freeRunning_) && currentTime_ >= stageTime_) {
            // advance through completed timed stages
            ASSERT(isReleased_ || !stageSustained_ || freeRunning_);
            if (stageTime_ == 0) {
                // if stage is of zero duration, immediate transition to level
                currentLevel_ = stageTargetLevel_;
            }
            if (!advanceToNextStage()) {
                out.remove_prefix(frameIndex);
                fill(out, 0.0f);
                return;
            }
        }

        // Process without going past the release point, if there is one
        size_t maxFrameIndex = numFrames;
        if (currentFramesUntilRelease_)
            maxFrameIndex = std::min(maxFrameIndex, frameIndex + *currentFramesUntilRelease_);

        // Process the current stage
        float time = currentTime_;
        float level = currentLevel_;
        const float stageEndTime = stageTime_;
        const float sourceLevel = stageSourceLevel_;
        const float targetLevel = stageTargetLevel_;
        const bool sustained = stageSustained_;
        const Curve& curve = *stageCurve_;
        size_t framesDone = 0;
        const float inverseTime = 1.0f / stageEndTime;
        if (frameIndex < maxFrameIndex && sustained && !freeRunning_ && !desc.dynamic
            && (time + samplePeriod) * inverseTime >= 1.0f) {
            // A sustain past the time of its stage, as mostly: the curve
            // stays at its end, every frame takes its level. The time counts
            // no more until the stage changes, which starts it from 0, so
            // that it only keeps growing (aud_dsp_sampler, ticket 30).
            time += samplePeriod;
            const float c = curve.evalNormalized(time * inverseTime);
            level = sourceLevel + c * (targetLevel - sourceLevel);
            framesDone = maxFrameIndex - frameIndex;
            fill(out.subspan(frameIndex, framesDone), level);
            frameIndex = maxFrameIndex;
            time += static_cast<float>(framesDone - 1) * samplePeriod;
        } else if (frameIndex < maxFrameIndex && (time < stageEndTime || sustained)) {
            // The times of the frames, summed as a loop sums them, then the
            // levels, which no longer wait for each other (ticket 30). The
            // frame whose time reaches the end of a timed stage is its last.
            float* frame = out.data() + frameIndex;
            const size_t room = maxFrameIndex - frameIndex;
            stepSum(time + samplePeriod, samplePeriod, frame, static_cast<unsigned>(room));
            size_t frames = room;
            // The times only grow: mostly the last one is before the end.
            if (!sustained && frame[room - 1] >= stageEndTime) {
                for (size_t i = 0; i < room; ++i) {
                    if (frame[i] >= stageEndTime) {
                        frames = i + 1;
                        break;
                    }
                }
            }
            time = frame[frames - 1];
            stageLevels(curve, inverseTime, sourceLevel, targetLevel, frame, frames);
            level = frame[frames - 1];
            framesDone = frames;
            frameIndex += frames;
        }
        currentLevel_ = level;

        // Update the counter to release
        if (currentFramesUntilRelease_)
            *currentFramesUntilRelease_ -= framesDone;

        currentTime_ = time;
    }
}

bool FlexEnvelope::Impl::advanceToStage(unsigned stageNumber)
{
    const FlexEGDescription& desc = *desc_;
    currentStageNumber_ = stageNumber;

    if (stageNumber >= desc.points.size())
        return false;

    const FlexEGPoint& point = desc.points[stageNumber];
    stageSourceLevel_ = currentLevel_;
    currentTime_ = 0.0f;
    updateCurrentTimeAndLevel();
    stageSustained_ = int(stageNumber) == desc.sustain;
    stageCurve_ = &point.curve();

    return true;
};

bool FlexEnvelope::Impl::advanceToNextStage()
{
    // The fade of a voice turned off is the last stage (aud_dsp_sampler,
    // ticket 27).
    if (offStage_)
        return advanceToStage(static_cast<unsigned>(desc_->points.size()));
    return advanceToStage(currentStageNumber_ + 1);
}

void FlexEnvelope::Impl::updateCurrentTimeAndLevel(int delay)
{
    const FlexEGDescription& desc = *desc_;
    if (offStage_ || currentStageNumber_ >= desc.points.size())
        return;

    const FlexEGPoint& point = desc.points[currentStageNumber_];
    const MidiState& midiState = resources_->getMidiState();
    stageTargetLevel_ = point.getLevel(midiState, delay);
    stageTime_ = point.getTime(midiState, delay);
}

// The fade of a voice turned off: one linear stage from the current level
// to zero, after which advanceToNextStage() finishes the envelope. It keeps
// the number of the stage it interrupts, so that it computes no stage
// number of its own, also for an EG without points.
void FlexEnvelope::Impl::enterOffStage()
{
    offStage_ = true;
    stageSourceLevel_ = currentLevel_;
    stageTargetLevel_ = 0.0f;
    stageTime_ = offTime_;
    currentTime_ = 0.0f;
    stageSustained_ = false;
    stageCurve_ = &Curve::getDefault();
}

} // namespace sfz

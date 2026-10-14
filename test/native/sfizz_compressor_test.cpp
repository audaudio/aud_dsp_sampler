// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the compressor of the sfizz fork (src/sfizz/effects/Compressor.cpp
// and effects/gen/compressor.hxx) against its original (ticket 28): it
// calls log10 only above a level just below its threshold and pow only for
// a gain other than 0 dB, and passes silence through while it is at rest.
// After a silent block it settles a level, a gain and a memory of its
// oversampling filters below their floors to 0, so that it comes to rest
// about a second after a loud sound, and a silent block whose filters are
// silent and whose level lies below the threshold only decays its
// detectors (release 0.3.2). The original lives on in this file: the Faust
// code of effects/gen/compressor.hxx as Faust 2.30.5 generated it
// (STK-4.3, see src/third_party/NOTICES.md) and the process function of
// effects/Compressor.cpp of version 0.3.0. On loud, quiet, silent,
// transient and stereo-linked signals and on sounds after pauses of every
// length, with the settings of an app's limiter and others, the outputs
// differ by at most 1e-6 (decision sampler-007 allows 1e-5); a signal
// without a silent block gives the same values, and only the sign of a zero
// may differ, which no later stage of the synth sees.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Effects.h"
#include "Opcode.h"
#include "OversamplerHelpers.h"
#include "ScopedFTZ.h"
#include "effects/Compressor.h"
#include "effects/gen/compressor.hxx"

#include "aud_test.hpp"

namespace {

// ############################################################################
// The originals

// effects/gen/compressor.hxx as generated, its compute() unchanged.
class OriginalFaustCompressor {
public:
    void instanceConstants(int sample_rate)
    {
        fSampleRate = sample_rate;
        fConst0 = (1.0f / float(fSampleRate));
    }
    void instanceResetUserInterface()
    {
        fHslider0 = float(0.0f);
        fHslider1 = float(1.0f);
        fHslider2 = float(0.0f);
        fHslider3 = float(0.0f);
    }
    void instanceClear()
    {
        for (int l0 = 0; (l0 < 2); l0 = (l0 + 1)) {
            fRec2[l0] = 0.0f;
        }
        for (int l1 = 0; (l1 < 2); l1 = (l1 + 1)) {
            fRec1[l1] = 0.0f;
        }
        for (int l2 = 0; (l2 < 2); l2 = (l2 + 1)) {
            fRec0[l2] = 0.0f;
        }
    }
    void compute(int count, float const* const* inputs, float* const* outputs)
    {
        float const* input0 = inputs[0];
        float* output0 = outputs[0];
        float fSlow0 = float(fHslider0);
        float fSlow1 = (0.5f * fSlow0);
        int iSlow2 = (std::fabs(fSlow1) < 1.1920929e-07f);
        float fSlow3 = (iSlow2 ? 0.0f : std::exp((0.0f - (fConst0 / (iSlow2 ? 1.0f : fSlow1)))));
        float fSlow4 = ((1.0f / std::max<float>(1.1920929e-07f, float(fHslider1))) + -1.0f);
        int iSlow5 = (std::fabs(fSlow0) < 1.1920929e-07f);
        float fSlow6 = (iSlow5 ? 0.0f : std::exp((0.0f - (fConst0 / (iSlow5 ? 1.0f : fSlow0)))));
        float fSlow7 = float(fHslider2);
        int iSlow8 = (std::fabs(fSlow7) < 1.1920929e-07f);
        float fSlow9 = (iSlow8 ? 0.0f : std::exp((0.0f - (fConst0 / (iSlow8 ? 1.0f : fSlow7)))));
        float fSlow10 = float(fHslider3);
        float fSlow11 = (1.0f - fSlow3);
        for (int i = 0; (i < count); i = (i + 1)) {
            float fTemp0 = float(input0[i]);
            float fTemp1 = std::fabs(fTemp0);
            float fTemp2 = ((fRec1[1] > fTemp1) ? fSlow9 : fSlow6);
            fRec2[0] = ((fRec2[1] * fTemp2) + (fTemp1 * (1.0f - fTemp2)));
            fRec1[0] = fRec2[0];
            fRec0[0] = ((fSlow3 * fRec0[1]) + (fSlow4 * (std::max<float>(((20.0f * std::log10(fRec1[0])) - fSlow10), 0.0f) * fSlow11)));
            output0[i] = float(std::pow(10.0f, (0.0500000007f * fRec0[0])));
            fRec2[1] = fRec2[0];
            fRec1[1] = fRec1[0];
            fRec0[1] = fRec0[0];
        }
    }
    void setRatio(float value) { fHslider1 = value; }
    void setThreshold(float value) { fHslider3 = value; }
    void setAttack(float value) { fHslider0 = value; }
    void setRelease(float value) { fHslider2 = value; }

private:
    float fHslider0;
    int fSampleRate;
    float fConst0;
    float fHslider1;
    float fHslider2;
    float fRec2[2];
    float fRec1[2];
    float fHslider3;
    float fRec0[2];
};

// The settings of a compressor, as the opcodes of an <effect> give them.
struct CompSettings {
    float attack = 0.005f;
    float release = 0.05f;
    float threshold = 0.0f;
    float ratio = 1.0f;
    float gainDb = 0.0f;
    bool stlink = false;

    std::vector<sfz::Opcode> opcodes() const
    {
        return {
            { "type", "comp" },
            { "comp_attack", std::to_string(attack) },
            { "comp_release", std::to_string(release) },
            { "comp_threshold", std::to_string(threshold) },
            { "comp_ratio", std::to_string(ratio) },
            { "comp_gain", std::to_string(gainDb) },
            { "comp_stlink", stlink ? "on" : "off" },
        };
    }
};

// Compressor::process of 0.3.0 with the original Faust code.
class OriginalCompressor {
public:
    OriginalCompressor(const CompSettings& settings, double sampleRate, int maxFrames)
    {
        for (OriginalFaustCompressor& comp : compressor_) {
            comp.instanceResetUserInterface();
            // The values as makeInstance reads them from the opcodes.
            comp.setAttack(read(settings.opcodes(), "comp_attack", sfz::Default::compAttack));
            comp.setRelease(read(settings.opcodes(), "comp_release", sfz::Default::compRelease));
            comp.setThreshold(read(settings.opcodes(), "comp_threshold", sfz::Default::compThreshold));
            comp.setRatio(read(settings.opcodes(), "comp_ratio", sfz::Default::compRatio));
            comp.instanceConstants(2 * sampleRate);
            comp.instanceClear();
        }
        inputGain_ = read(settings.opcodes(), "comp_gain", sfz::Default::compGain);
        stlink_ = settings.stlink;
        for (int c = 0; c < 2; ++c) {
            downsampler2x_[c].set_coefs(sfz::OSCoeffs2x);
            upsampler2x_[c].set_coefs(sfz::OSCoeffs2x);
            temp2x_[c].resize(2 * maxFrames);
            gain2x_[c].resize(2 * maxFrames);
        }
    }

    void process(const float* const inputs[], float* const outputs[], unsigned nframes)
    {
        float* left2x = temp2x_[0].data();
        float* right2x = temp2x_[1].data();
        upsampler2x_[0].process_block(left2x, inputs[0], nframes);
        upsampler2x_[1].process_block(right2x, inputs[1], nframes);
        const float inputGain = inputGain_;
        for (unsigned i = 0; i < 2 * nframes; ++i) {
            left2x[i] *= inputGain;
            right2x[i] *= inputGain;
        }
        if (!stlink_) {
            float* leftGain2x = gain2x_[0].data();
            float* rightGain2x = gain2x_[1].data();
            {
                float* in[] = { left2x };
                float* out[] = { leftGain2x };
                compressor_[0].compute(2 * nframes, in, out);
            }
            {
                float* in[] = { right2x };
                float* out[] = { rightGain2x };
                compressor_[1].compute(2 * nframes, in, out);
            }
            for (unsigned i = 0; i < 2 * nframes; ++i) {
                left2x[i] *= leftGain2x[i];
                right2x[i] *= rightGain2x[i];
            }
        } else {
            float* compIn2x = gain2x_[0].data();
            for (unsigned i = 0; i < 2 * nframes; ++i)
                compIn2x[i] = std::abs(left2x[i]) + std::abs(right2x[i]);
            float* gain2x = gain2x_[1].data();
            {
                float* in[] = { compIn2x };
                float* out[] = { gain2x };
                compressor_[0].compute(2 * nframes, in, out);
            }
            for (unsigned i = 0; i < 2 * nframes; ++i) {
                left2x[i] *= gain2x[i];
                right2x[i] *= gain2x[i];
            }
        }
        downsampler2x_[0].process_block(outputs[0], left2x, nframes);
        downsampler2x_[1].process_block(outputs[1], right2x, nframes);
    }

private:
    template <class T>
    static T read(const std::vector<sfz::Opcode>& opcodes, const char* name, sfz::OpcodeSpec<T> spec)
    {
        T value = spec;
        for (const sfz::Opcode& opcode : opcodes) {
            if (opcode.name == name)
                value = opcode.read(spec);
        }
        return value;
    }

    OriginalFaustCompressor compressor_[2];
    bool stlink_ = false;
    float inputGain_ = 1.0f;
    std::vector<float> temp2x_[2];
    std::vector<float> gain2x_[2];
    hiir::Downsampler2x<12> downsampler2x_[2];
    hiir::Upsampler2x<12> upsampler2x_[2];
};

// ############################################################################
// Signals and comparisons

constexpr double kSampleRate = 48000;
constexpr int kMaxFrames = 256;

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
    size_t size() const { return left.size(); }
};

void append(Stereo& signal, size_t frames, float amplitudeLeft, float amplitudeRight, float frequency)
{
    const size_t start = signal.size();
    for (size_t i = 0; i < frames; ++i) {
        const double phase = 2 * M_PI * frequency * static_cast<double>(start + i) / kSampleRate;
        signal.left.push_back(static_cast<float>(amplitudeLeft * std::sin(phase)));
        signal.right.push_back(static_cast<float>(amplitudeRight * std::sin(phase * 1.01)));
    }
}

void appendNoise(Stereo& signal, size_t frames, float amplitude, std::mt19937& random)
{
    std::normal_distribution<float> noise(0.0f, amplitude);
    for (size_t i = 0; i < frames; ++i) {
        signal.left.push_back(noise(random));
        signal.right.push_back(noise(random));
    }
}

// The largest difference the compressor may make to the output of the
// original: the settled state moves it by about one step of a float near 1.
constexpr double kTolerance = 1e-6;

// The signals the tests compress: loud and quiet tones, silence, clicks
// between long silences, one channel alone, noise around the threshold, and
// sounds after pauses of every length.
std::vector<std::pair<std::string, Stereo>> signals()
{
    std::mt19937 random(28);
    std::vector<std::pair<std::string, Stereo>> result;
    Stereo loud;
    append(loud, 48000, 2.0f, 1.5f, 220.0f);
    result.emplace_back("loud", loud);
    Stereo quiet;
    append(quiet, 48000, 0.2f, 0.1f, 330.0f);
    result.emplace_back("quiet", quiet);
    Stereo silent;
    append(silent, 48000, 0.0f, 0.0f, 0.0f);
    result.emplace_back("silent", silent);
    // Clicks: silence, a loud burst, silence long enough for the
    // compressor to come to rest, a burst again.
    Stereo transients;
    append(transients, 9600, 0.0f, 0.0f, 0.0f);
    append(transients, 240, 3.0f, 2.0f, 1000.0f);
    append(transients, 6 * 48000, 0.0f, 0.0f, 0.0f);
    append(transients, 240, 1.5f, 0.5f, 2000.0f);
    append(transients, 48000, 0.0f, 0.0f, 0.0f);
    result.emplace_back("transients", transients);
    // One channel loud, the other silent: what the stereo link joins.
    Stereo oneSided;
    append(oneSided, 24000, 1.8f, 0.0f, 440.0f);
    append(oneSided, 24000, 0.0f, 1.8f, 440.0f);
    result.emplace_back("one-sided", oneSided);
    Stereo noise;
    appendNoise(noise, 48000, 0.5f, random);
    result.emplace_back("noise", noise);
    // Tones, and clicks near an app's threshold, after pauses that end
    // while the level falls through the threshold, before and after an
    // app's limiter comes to rest, and before and after one with a release
    // of 0.2 s does: after 4 s, a tone at the threshold of -30 dB meets a
    // level the original still holds and the fork settled, and its output
    // moves by one step of a float near 1.5, 1.2e-7.
    Stereo pauses;
    const float gaps[] = { 0.02f, 0.1f, 0.3f, 0.6f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 7.0f };
    const float levels[] = { 2.0f, 0.95f, 1e-3f, 0.5f, 1.2f, 0.94f, 3.0f, 1e-5f, 0.9f, 1.0f, 0.97f, 2.0f };
    for (int k = 0; k < 12; ++k) {
        append(pauses, 4800, levels[k], levels[(k + 3) % 12], 220.0f + 50 * k);
        append(pauses, static_cast<size_t>(gaps[k] * kSampleRate), 0.0f, 0.0f, 0.0f);
        append(pauses, 96, 0.96f, 0.96f, 3000.0f);
        append(pauses, static_cast<size_t>(gaps[(k + 5) % 12] * kSampleRate), 0.0f, 0.0f, 0.0f);
    }
    result.emplace_back("pauses", pauses);
    return result;
}

// The block sizes the engine renders in turn: an app's 180 frames, and
// shorter and longer ones.
const std::vector<unsigned> kBlocks = { 180, 1, 17, 64, 256, 3, 180, 100 };

// The differences between two outputs: the values that differ, and the
// largest difference; NaN against NaN is no difference.
struct Difference {
    size_t values = 0;
    double largest = 0;
};

void compare(const float* a, const float* b, size_t frames, Difference& difference)
{
    for (size_t i = 0; i < frames; ++i) {
        if (a[i] == b[i] || (std::isnan(a[i]) && std::isnan(b[i])))
            continue;
        difference.values += 1;
        const double by = std::fabs(static_cast<double>(a[i]) - b[i]);
        difference.largest = std::max(difference.largest, std::isnan(by) ? INFINITY : by);
    }
}

std::unique_ptr<sfz::Effect> makeCompressor(const CompSettings& settings)
{
    const std::vector<sfz::Opcode> opcodes = settings.opcodes();
    std::unique_ptr<sfz::Effect> effect = sfz::fx::Compressor::makeInstance(opcodes);
    effect->setSampleRate(kSampleRate);
    effect->setSamplesPerBlock(kMaxFrames);
    return effect;
}

// What a comparison of the compressors saw besides the difference:
// whether the compressor was at rest after a block, whether it came back to
// rest after it had sound, and whether a block was silent.
struct Course {
    bool rested = false;
    bool returned = false;
    bool silentBlock = false;
};

// Compresses `signal` with the fork's compressor and the original in blocks
// of kBlocks, every other block in place; returns how the outputs differ.
Difference compareCompressors(const CompSettings& settings, const Stereo& signal, Course& course)
{
    std::unique_ptr<sfz::Effect> effect = makeCompressor(settings);
    OriginalCompressor original(settings, kSampleRate, kMaxFrames);
    Difference difference;
    std::vector<float> in[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> out[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> expected[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    course = Course();
    bool stirred = false;
    size_t block = 0;
    for (size_t done = 0; done < signal.size();) {
        const unsigned frames = static_cast<unsigned>(std::min<size_t>(kBlocks[block % kBlocks.size()], signal.size() - done));
        const bool inPlace = block % 2 == 1;
        for (int c = 0; c < 2; ++c) {
            const std::vector<float>& source = c == 0 ? signal.left : signal.right;
            std::copy(source.begin() + done, source.begin() + done + frames, in[c].begin());
        }
        const float* inputs[2] = { in[0].data(), in[1].data() };
        course.silentBlock = course.silentBlock || sfz::isSilent(inputs, frames);
        float* expectedPointers[2] = { expected[0].data(), expected[1].data() };
        {
            // The synth renders with denormals flushed to zero.
            ScopedFTZ ftz;
            original.process(inputs, expectedPointers, frames);
            if (inPlace) {
                float* inOut[2] = { in[0].data(), in[1].data() };
                effect->process(inOut, inOut, frames);
            } else {
                float* outputs[2] = { out[0].data(), out[1].data() };
                effect->process(inputs, outputs, frames);
            }
        }
        for (int c = 0; c < 2; ++c)
            compare(inPlace ? in[c].data() : out[c].data(), expected[c].data(), frames, difference);
        const bool atRest = effect->isAtRest();
        course.rested = course.rested || atRest;
        course.returned = course.returned || (stirred && atRest);
        stirred = stirred || !atRest;
        done += frames;
        block += 1;
    }
    return difference;
}

// The settings of an app's limiter, with a release of `release` seconds.
CompSettings appLimiter(float release)
{
    CompSettings app;
    app.attack = 0;
    app.release = release;
    app.ratio = 50;
    app.threshold = -0.5f;
    return app;
}

// The settings of the compressors: an app's limiter, linked and not, and
// others with an attack, a gain and the extremes of the opcodes.
std::vector<std::pair<std::string, CompSettings>> compSettings()
{
    std::vector<std::pair<std::string, CompSettings>> result;
    CompSettings app = appLimiter(0.05f);
    result.emplace_back("app limiter", app);
    app.stlink = true;
    result.emplace_back("app limiter, linked", app);
    CompSettings attack;
    attack.attack = 0.001f;
    attack.release = 0.05f;
    attack.ratio = 8;
    attack.threshold = -20;
    result.emplace_back("attack 1 ms, -20 dB, 8:1", attack);
    CompSettings gain;
    gain.attack = 0.01f;
    gain.release = 0.2f;
    gain.ratio = 4;
    gain.threshold = -30;
    gain.gainDb = 6;
    gain.stlink = true;
    result.emplace_back("gain 6 dB, -30 dB, 4:1, linked", gain);
    CompSettings unity;
    unity.threshold = 0;
    unity.ratio = 1;
    result.emplace_back("0 dB, 1:1", unity);
    CompSettings floor;
    floor.attack = 0;
    floor.release = 0;
    floor.threshold = -100;
    floor.ratio = 50;
    result.emplace_back("-100 dB, 50:1, no release", floor);
    return result;
}

// The term of the threshold as the original computes it.
float originalTerm(float level, float threshold)
{
    return std::max<float>(((20.0f * std::log10(level)) - threshold), 0.0f);
}

// The level at or below which the fork skips log10, as compressor.hxx
// computes it.
float skipBelowOf(float threshold)
{
    const double level = std::pow(10.0, double(threshold) / 20.0) * (1.0 - 1e-4);
    return float(std::min(level, double(std::numeric_limits<float>::max())));
}

float floatOfBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t bitsOf(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// The seconds a compressor takes to come to rest in silence after half a
// second of a stereo tone of `amplitude`, in blocks of an app's 180 frames;
// -1 if it does not within 20 s.
double secondsToRest(const CompSettings& settings, float amplitude)
{
    constexpr unsigned kFrames = 180;
    std::unique_ptr<sfz::Effect> effect = makeCompressor(settings);
    std::vector<float> in[2] = { std::vector<float>(kFrames), std::vector<float>(kFrames) };
    std::vector<float> out[2] = { std::vector<float>(kFrames), std::vector<float>(kFrames) };
    const float* inputs[2] = { in[0].data(), in[1].data() };
    float* outputs[2] = { out[0].data(), out[1].data() };
    ScopedFTZ ftz;
    const size_t burst = static_cast<size_t>(kSampleRate / 2);
    for (size_t done = 0; done < burst; done += kFrames) {
        for (unsigned i = 0; i < kFrames; ++i) {
            const double t = static_cast<double>(done + i) / kSampleRate;
            in[0][i] = amplitude * static_cast<float>(std::sin(2 * M_PI * 220 * t));
            in[1][i] = amplitude * static_cast<float>(std::sin(2 * M_PI * 222 * t));
        }
        effect->process(inputs, outputs, kFrames);
    }
    for (int c = 0; c < 2; ++c)
        std::fill(in[c].begin(), in[c].end(), 0.0f);
    for (size_t blocks = 0; blocks * kFrames < 20 * kSampleRate; ++blocks) {
        if (effect->isAtRest())
            return blocks * static_cast<double>(kFrames) / kSampleRate;
        effect->process(inputs, outputs, kFrames);
    }
    return -1;
}

// The Faust compressor of the effect with `settings`, at the oversampled
// rate and cleared, as the effect sets it up.
faustCompressor faustOf(const CompSettings& settings)
{
    faustCompressor comp;
    comp.instanceResetUserInterface();
    comp.setAttack(settings.attack);
    comp.setRelease(settings.release);
    comp.setThreshold(settings.threshold);
    comp.setRatio(settings.ratio);
    comp.instanceConstants(2 * static_cast<int>(kSampleRate));
    comp.instanceClear();
    return comp;
}

// Whether two states of a Faust compressor hold the same values; the sign
// of a zero does not count.
bool sameState(const faustCompressor::State& a, const faustCompressor::State& b)
{
    return a.fRec2 == b.fRec2 && a.fRec1 == b.fRec1 && a.fRec0 == b.fRec0;
}

// Runs compute() of `comp` on `count` samples of silence.
void computeSilence(faustCompressor& comp, int count)
{
    std::vector<float> zeros(count, 0.0f);
    std::vector<float> gains(count);
    const float* inputs[] = { zeros.data() };
    float* outputs[] = { gains.data() };
    comp.compute(count, inputs, outputs);
}

} // namespace

// ############################################################################
// The compressor

AUD_TEST(sfizz_compressor_skips_log10_only_where_the_original_term_is_zero) {
    // Every level at or below the one the fork skips log10 for gives the
    // term +0 in the original, the value the fork takes: the 2^22 floats
    // below that level, the closest to the threshold, and every 997th
    // further below, down to 0.
    for (float threshold : { -0.5f, 0.0f, -6.0f, -20.0f, -60.0f, -100.0f }) {
        const float skipBelow = skipBelowOf(threshold);
        AUD_CHECK(skipBelow > 0.0f);
        // Just above, the term may not be 0: the fork computes it there.
        AUD_CHECK(originalTerm(skipBelow / float(1.0 - 1e-4) * 1.001f, threshold) > 0.0f);
        size_t checked = 0;
        size_t nonZero = 0;
        const uint32_t top = bitsOf(skipBelow);
        const uint32_t dense = top - (1u << 22);
        for (uint32_t bits = top;; bits -= (bits > dense ? 1 : 997)) {
            const float term = originalTerm(floatOfBits(bits), threshold);
            if (bitsOf(term) != 0)
                nonZero += 1;
            checked += 1;
            if (bits < 997)
                break;
        }
        AUD_CHECK(bitsOf(originalTerm(0.0f, threshold)) == 0);
        AUD_CHECK(checked > (1u << 22));
        AUD_CHECK(nonZero == 0);
    }
}

AUD_TEST(sfizz_compressor_compresses_like_the_original) {
    // Within 1e-6 of the original on every signal; a signal without a silent
    // block, after which nothing settles, gives the same values. Silence
    // keeps every compressor at rest, where it passes silence through
    // without processing it, and the pause of 6 s brings every one back to
    // rest before the next click.
    const std::vector<std::pair<std::string, Stereo>> inputs = signals();
    for (const auto& settings : compSettings()) {
        for (const auto& signal : inputs) {
            Course course;
            const Difference difference = compareCompressors(settings.second, signal.second, course);
            AUD_CHECK(difference.largest <= kTolerance);
            if (!course.silentBlock)
                AUD_CHECK(difference.values == 0);
            if (signal.first == "silent")
                AUD_CHECK(course.rested);
            if (signal.first == "transients")
                AUD_CHECK(course.returned);
        }
    }
}

AUD_TEST(sfizz_compressor_comes_to_rest_soon_after_a_loud_sound) {
    // Half a second of a tone at twice full scale, then silence: an app's
    // limiter comes to rest after about a second, with a release of 0.2 s
    // after about 4.5 s, and every other setting too, the one with a gain
    // of 6 dB that doubles the level last. Release 0.3.1 waited until the
    // detector fell below the smallest float: 4.4 s for the app's limiter,
    // 17.9 s for the gain of 6 dB.
    const double app = secondsToRest(appLimiter(0.05f), 2.0f);
    AUD_CHECK(app > 0 && app <= 1.1);
    const double slow = secondsToRest(appLimiter(0.2f), 2.0f);
    AUD_CHECK(slow > 0 && slow <= 4.5);
    for (const auto& settings : compSettings()) {
        const double seconds = secondsToRest(settings.second, 2.0f);
        AUD_CHECK(seconds > 0 && seconds <= 4.6);
    }
}

AUD_TEST(sfizz_compressor_is_at_rest_only_when_silence_stays_silent) {
    std::unique_ptr<sfz::Effect> effect = makeCompressor(appLimiter(0.05f));
    AUD_CHECK(effect->isAtRest());
    std::vector<float> in[2] = { std::vector<float>(kMaxFrames, 0.0f), std::vector<float>(kMaxFrames, 0.0f) };
    std::vector<float> out[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    const float* inputs[2] = { in[0].data(), in[1].data() };
    float* outputs[2] = { out[0].data(), out[1].data() };
    ScopedFTZ ftz;
    // Silence keeps it at rest and passes as silence.
    effect->process(inputs, outputs, kMaxFrames);
    AUD_CHECK(effect->isAtRest());
    AUD_CHECK(sfz::isSilent(outputs, kMaxFrames));
    // A quiet sample stirs the filters and the detector.
    in[0][10] = 1e-3f;
    effect->process(inputs, outputs, kMaxFrames);
    AUD_CHECK(!effect->isAtRest());
    // Silence brings it back to rest within a second, with denormals
    // flushed to zero as the synth renders: the detector falls with its
    // release of 50 ms below its floor of 1e-9 and settles to 0, and the
    // output stays silent.
    in[0][10] = 0.0f;
    int blocks = 0;
    bool silentOutput = true;
    while (!effect->isAtRest() && blocks < 2000) {
        effect->process(inputs, outputs, kMaxFrames);
        silentOutput = silentOutput && std::fabs(out[0][0]) < 1e-3f;
        blocks += 1;
    }
    AUD_CHECK(effect->isAtRest());
    AUD_CHECK(blocks * kMaxFrames < 48000);
    AUD_CHECK(silentOutput);
    // clear() resets the detector but, as in the original, not the
    // memory of the oversampling filters, which the silence that follows
    // brings to rest.
    in[0][0] = 1.0f;
    effect->process(inputs, outputs, kMaxFrames);
    in[0][0] = 0.0f;
    effect->clear();
    AUD_CHECK(!effect->isAtRest());
    for (int i = 0; i < 2000 && !effect->isAtRest(); ++i)
        effect->process(inputs, outputs, kMaxFrames);
    AUD_CHECK(effect->isAtRest());
}

// ############################################################################
// The detectors in silence

AUD_TEST(sfizz_compressor_decays_silence_like_its_loop) {
    // A loud tone on the left detector, a quieter one on the right, then
    // silence in blocks of every size: wherever a detector may decay without
    // its loop, its level lies at or below the threshold, and decaySilence()
    // and decaySilenceTwo() leave the state the loop leaves, value for value,
    // the gain of an attack among it.
    for (const auto& settings : compSettings()) {
        ScopedFTZ ftz;
        faustCompressor loop[2] = { faustOf(settings.second), faustOf(settings.second) };
        for (int c = 0; c < 2; ++c) {
            std::vector<float> tone(9600);
            std::vector<float> gains(tone.size());
            for (size_t i = 0; i < tone.size(); ++i)
                tone[i] = (c == 0 ? 2.0f : 0.3f) * static_cast<float>(std::sin(2 * M_PI * 220 * i / (2 * kSampleRate)));
            const float* toneInputs[] = { tone.data() };
            float* gainOutputs[] = { gains.data() };
            loop[c].compute(static_cast<int>(tone.size()), toneInputs, gainOutputs);
        }
        faustCompressor fast[2] = { loop[0], loop[1] };
        const float skipBelow = skipBelowOf(settings.second.threshold);
        size_t decayed = 0;
        size_t refused = 0;
        size_t differing = 0;
        for (size_t block = 0; block < 2000; ++block) {
            const int count = 2 * static_cast<int>(kBlocks[block % kBlocks.size()]);
            faustCompressor::Slow k[2] = { fast[0].slow(), fast[1].slow() };
            bool can[2];
            for (int c = 0; c < 2; ++c) {
                can[c] = fast[c].canDecaySilence(k[c]);
                AUD_CHECK(can[c] == (fast[c].state().fRec2 <= skipBelow));
            }
            if (can[0] && can[1]) {
                faustCompressor::decaySilenceTwo(fast[0], k[0], fast[1], k[1], count);
            } else {
                for (int c = 0; c < 2; ++c) {
                    if (can[c])
                        fast[c].decaySilence(k[c], count);
                    else
                        computeSilence(fast[c], count);
                }
            }
            decayed += can[0] ? 1 : 0;
            refused += can[0] ? 0 : 1;
            for (int c = 0; c < 2; ++c) {
                computeSilence(loop[c], count);
                differing += sameState(fast[c].state(), loop[c].state()) ? 0 : 1;
            }
        }
        AUD_CHECK(decayed > 0);
        AUD_CHECK(refused > 0);
        AUD_CHECK(differing == 0);
    }
}

AUD_TEST(sfizz_compressor_decays_silence_only_where_the_loop_gives_silence) {
    // A level above the threshold, a level or a gain that is not a number,
    // a gain whose factor would overflow and a negative attack, whose
    // coefficient exceeds 1: the loop runs.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    faustCompressor comp = faustOf(appLimiter(0.05f));
    auto can = [&comp](faustCompressor::State state) {
        comp.setState(state);
        return comp.canDecaySilence(comp.slow());
    };
    AUD_CHECK(can({ 0.5f, 0.5f, -6.0f }));
    AUD_CHECK(can({ 0.0f, 0.0f, 0.0f }));
    AUD_CHECK(!can({ 0.95f, 0.95f, 0.0f }));
    AUD_CHECK(!can({ nan, nan, 0.0f }));
    AUD_CHECK(!can({ 0.5f, 0.5f, nan }));
    AUD_CHECK(!can({ 0.5f, 0.5f, 800.0f }));
    AUD_CHECK(!can({ -0.5f, -0.5f, 0.0f }));
    comp.setAttack(-1.0f);
    AUD_CHECK(!can({ 0.5f, 0.5f, 0.0f }));
}

AUD_TEST(sfizz_compressor_settles_only_what_lies_below_its_floors) {
    // A level of at most 1e-9 and a gain within 1e-7 dB of 0 dB become 0,
    // each on its own; above the floors and NaN stay.
    faustCompressor comp = faustOf(appLimiter(0.05f));
    auto settled = [&comp](faustCompressor::State state) {
        comp.setState(state);
        comp.settle(1e-9f, 1e-7f);
        return comp.state();
    };
    AUD_CHECK(sameState(settled({ 1e-9f, 1e-9f, -1e-7f }), { 0.0f, 0.0f, 0.0f }));
    AUD_CHECK(comp.isAtRest());
    AUD_CHECK(sameState(settled({ 0.0f, 0.0f, 1e-7f }), { 0.0f, 0.0f, 0.0f }));
    AUD_CHECK(sameState(settled({ 2e-9f, 2e-9f, -5e-8f }), { 2e-9f, 2e-9f, 0.0f }));
    AUD_CHECK(!comp.isAtRest());
    AUD_CHECK(sameState(settled({ 5e-10f, 5e-10f, -2e-7f }), { 0.0f, 0.0f, -2e-7f }));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const faustCompressor::State kept = settled({ nan, nan, nan });
    AUD_CHECK(std::isnan(kept.fRec2) && std::isnan(kept.fRec1) && std::isnan(kept.fRec0));
}

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the compressor and the gain of the sfizz fork against their
// originals (ticket 28): the compressor calls log10 only above a level just
// below its threshold and pow only for a gain other than 0 dB, passes
// silence through while it is at rest, and a bus whose input is silent
// skips its effects at rest; the gain effect converts its gain once per
// block. The originals live on in this file: the Faust code of
// effects/gen/compressor.hxx as Faust 2.30.5 generated it (STK-4.3, see
// src/third_party/NOTICES.md) and the process functions of
// effects/Compressor.cpp and effects/Gain.cpp of version 0.3.0. On loud,
// quiet, silent, transient and stereo-linked signals, with the settings of
// an app's limiter and others, the outputs are the same, bit for bit; only
// the sign of a zero may differ, which no later stage of the synth sees.

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
#include "MathHelpers.h"
#include "Opcode.h"
#include "OversamplerHelpers.h"
#include "SfzHelpers.h"
#include "ScopedFTZ.h"
#include "effects/Compressor.h"
#include "effects/Gain.h"

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

// Gain::process of 0.3.0: the gain converted for every sample.
void originalGain(float gainDb, const float* const inputs[], float* const outputs[], unsigned nframes)
{
    std::vector<float> gains(nframes, gainDb);
    for (unsigned i = 0; i < nframes; ++i)
        gains[i] = std::pow(10.0f, 0.05f * gains[i]);
    for (unsigned c = 0; c < 2; ++c) {
        absl::Span<const float> input { inputs[c], nframes };
        absl::Span<float> output { outputs[c], nframes };
        sfz::applyGain(absl::Span<const float> { gains }, input, output);
    }
}

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

// The signals the tests compress: loud and quiet tones, silence, clicks
// between long silences, one channel alone, and noise around the threshold.
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
    return result;
}

// The block sizes the engine renders in turn: an app's 180 frames, and
// shorter and longer ones.
const std::vector<unsigned> kBlocks = { 180, 1, 17, 64, 256, 3, 180, 100 };

// The differences between two outputs: in bits, and in value.
struct Difference {
    size_t bits = 0;
    size_t values = 0;
};

void compare(const float* a, const float* b, size_t frames, Difference& difference)
{
    for (size_t i = 0; i < frames; ++i) {
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
            difference.bits += 1;
        if (!(a[i] == b[i]) && !(std::isnan(a[i]) && std::isnan(b[i])))
            difference.values += 1;
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

// Compresses `signal` with the fork's compressor and the original in blocks
// of kBlocks, every other block in place; returns how the outputs differ.
// `rested` tells whether the compressor was at rest after a block, `returned`
// whether it came back to rest after it had sound.
Difference compareCompressors(const CompSettings& settings, const Stereo& signal, bool& rested, bool& returned)
{
    std::unique_ptr<sfz::Effect> effect = makeCompressor(settings);
    OriginalCompressor original(settings, kSampleRate, kMaxFrames);
    Difference difference;
    std::vector<float> in[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> out[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> expected[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    rested = false;
    returned = false;
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
        rested = rested || atRest;
        returned = returned || (stirred && atRest);
        stirred = stirred || !atRest;
        done += frames;
        block += 1;
    }
    return difference;
}

// The settings of the compressors: an app's limiter, linked and not, and
// others with an attack, a gain and the extremes of the opcodes.
std::vector<std::pair<std::string, CompSettings>> compSettings()
{
    std::vector<std::pair<std::string, CompSettings>> result;
    CompSettings app;
    app.attack = 0;
    app.release = 0.05f;
    app.ratio = 50;
    app.threshold = -0.5f;
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
    const std::vector<std::pair<std::string, Stereo>> inputs = signals();
    for (const auto& settings : compSettings()) {
        for (const auto& signal : inputs) {
            bool rested = false;
            bool returned = false;
            const Difference difference = compareCompressors(settings.second, signal.second, rested, returned);
            if (difference.values != 0 || difference.bits != 0) {
                std::printf("  %s, %s: %zu values and %zu bit patterns differ\n", settings.first.c_str(),
                    signal.first.c_str(), difference.values, difference.bits);
            }
            AUD_CHECK(difference.values == 0);
            AUD_CHECK(difference.bits == 0);
            // Silence keeps every compressor at rest, where it passes silence
            // through without processing it, and the pause of 6 s brings one
            // with a release up to 50 ms back to rest before the next click;
            // its detector falls below the smallest float in about 4 s.
            if (signal.first == "silent")
                AUD_CHECK(rested);
            if (signal.first == "transients" && settings.second.release <= 0.05f) {
                if (!returned)
                    std::printf("  %s did not come back to rest\n", settings.first.c_str());
                AUD_CHECK(returned);
            }
        }
    }
}

AUD_TEST(sfizz_compressor_is_at_rest_only_when_silence_stays_silent) {
    CompSettings app;
    app.attack = 0;
    app.release = 0.05f;
    app.ratio = 50;
    app.threshold = -0.5f;
    std::unique_ptr<sfz::Effect> effect = makeCompressor(app);
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
    // Silence brings it back to rest within seconds, with denormals
    // flushed to zero as the synth renders: the detector falls with its
    // release of 50 ms below the smallest float in about 4 s.
    in[0][10] = 0.0f;
    int blocks = 0;
    while (!effect->isAtRest() && blocks < 2000) {
        effect->process(inputs, outputs, kMaxFrames);
        blocks += 1;
    }
    AUD_CHECK(effect->isAtRest());
    AUD_CHECK(blocks * kMaxFrames < 5 * 48000);
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
// The gain

AUD_TEST(sfizz_gain_effect_gains_like_the_original) {
    std::mt19937 random(28);
    Stereo signal;
    appendNoise(signal, 4096, 0.5f, random);
    for (float gainDb : { 7.35f, 0.0f, -6.0f, -144.0f, 48.0f, 0.1f }) {
        const std::vector<sfz::Opcode> opcodes = { { "type", "gain" }, { "gain", std::to_string(gainDb) } };
        std::unique_ptr<sfz::Effect> effect = sfz::fx::Gain::makeInstance(opcodes);
        effect->setSampleRate(kSampleRate);
        effect->setSamplesPerBlock(kMaxFrames);
        AUD_CHECK(effect->isAtRest());
        const float readDb = sfz::Opcode("gain", std::to_string(gainDb)).read(sfz::Default::volume);
        Difference difference;
        std::vector<float> out[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
        std::vector<float> expected[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
        for (size_t done = 0; done + kMaxFrames <= signal.size(); done += kMaxFrames) {
            const float* inputs[2] = { signal.left.data() + done, signal.right.data() + done };
            float* outputs[2] = { out[0].data(), out[1].data() };
            float* expectedPointers[2] = { expected[0].data(), expected[1].data() };
            effect->process(inputs, outputs, kMaxFrames);
            originalGain(readDb, inputs, expectedPointers, kMaxFrames);
            for (int c = 0; c < 2; ++c)
                compare(out[c].data(), expected[c].data(), kMaxFrames, difference);
        }
        AUD_CHECK(difference.bits == 0);
    }
    // A gain the opcode allows beyond its range, so large that silence
    // would become NaN, is not at rest.
    const std::vector<sfz::Opcode> huge = { { "type", "gain" }, { "gain", "1000" } };
    AUD_CHECK(!sfz::fx::Gain::makeInstance(huge)->isAtRest());
}

// ############################################################################
// The bus

AUD_TEST(sfizz_effect_bus_skips_silent_effects_at_rest_like_the_original) {
    // An app's output stage on the main bus: the limiter, then the gain.
    // Blocks of a tone, of nothing at all, of zeros a voice added and long
    // pauses: the bus skips its effects only where they would give silence,
    // and gives what the original effects give.
    CompSettings app;
    app.attack = 0;
    app.release = 0.05f;
    app.ratio = 50;
    app.threshold = -0.5f;
    sfz::EffectBus bus;
    bus.addEffect(sfz::fx::Compressor::makeInstance(app.opcodes()));
    const std::vector<sfz::Opcode> gainOpcodes = { { "type", "gain" }, { "gain", "7.35" } };
    bus.addEffect(sfz::fx::Gain::makeInstance(gainOpcodes));
    bus.setGainToMain(1.0f);
    bus.setGainToMix(0.0f);
    bus.setSampleRate(kSampleRate);
    bus.setSamplesPerBlock(kMaxFrames);
    OriginalCompressor original(app, kSampleRate, kMaxFrames);
    const float gainDb = sfz::Opcode("gain", "7.35").read(sfz::Default::volume);

    Stereo signal;
    append(signal, 4800, 0.0f, 0.0f, 0.0f);
    append(signal, 9600, 2.5f, 2.0f, 220.0f);
    append(signal, 6 * 48000, 0.0f, 0.0f, 0.0f);
    append(signal, 4800, 0.3f, 0.3f, 440.0f);
    append(signal, 6 * 48000, 0.0f, 0.0f, 0.0f);

    Difference difference;
    std::vector<float> in[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> main[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> mix[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    std::vector<float> expected[2] = { std::vector<float>(kMaxFrames), std::vector<float>(kMaxFrames) };
    size_t block = 0;
    for (size_t done = 0; done < signal.size(); block += 1) {
        const unsigned frames = static_cast<unsigned>(std::min<size_t>(kBlocks[block % kBlocks.size()], signal.size() - done));
        bool silent = true;
        for (int c = 0; c < 2; ++c) {
            const std::vector<float>& source = c == 0 ? signal.left : signal.right;
            std::copy(source.begin() + done, source.begin() + done + frames, in[c].begin());
            for (unsigned i = 0; i < frames; ++i)
                silent = silent && in[c][i] == 0.0f;
            std::fill(main[c].begin(), main[c].end(), 0.0f);
            std::fill(mix[c].begin(), mix[c].end(), 0.0f);
        }
        const float* inputs[2] = { in[0].data(), in[1].data() };
        float* mainPointers[2] = { main[0].data(), main[1].data() };
        float* mixPointers[2] = { mix[0].data(), mix[1].data() };
        float* expectedPointers[2] = { expected[0].data(), expected[1].data() };
        {
            ScopedFTZ ftz;
            bus.clearInputs(frames);
            // A silent block reaches the bus as no voice at all, or as the
            // zeros of a voice that ended.
            if (!silent || block % 3 == 0)
                bus.addToInputs(inputs, 1.0f, frames);
            bus.process(frames);
            bus.mixOutputsTo(mainPointers, mixPointers, frames);
            original.process(inputs, expectedPointers, frames);
            originalGain(gainDb, expectedPointers, expectedPointers, frames);
        }
        for (int c = 0; c < 2; ++c) {
            // The main output of the original bus: 0 plus the effects' output.
            for (unsigned i = 0; i < frames; ++i)
                expected[c][i] = 0.0f + 1.0f * expected[c][i];
            compare(main[c].data(), expected[c].data(), frames, difference);
        }
        done += frames;
    }
    AUD_CHECK(difference.bits == 0);
}

// ############################################################################
// Conversions per sample

AUD_TEST(sfizz_same_argument_cache_gives_the_results_of_every_call) {
    // Runs of equal arguments, both zeros, NaN and denormals: the cached
    // conversions give the bits the calls give.
    std::mt19937 random(28);
    std::uniform_real_distribution<float> cents(-2400.0f, 2400.0f);
    std::uniform_int_distribution<int> runs(1, 40);
    std::vector<float> arguments;
    while (arguments.size() < 100000) {
        float value = cents(random);
        switch (arguments.size() % 7) {
        case 0: value = 0.0f; break;
        case 1: value = -0.0f; break;
        case 2: value = std::numeric_limits<float>::quiet_NaN(); break;
        case 3: value = std::numeric_limits<float>::denorm_min(); break;
        default: break;
        }
        arguments.insert(arguments.end(), runs(random), value);
    }
    auto ratioOfCents = cacheSameArgument([](float c) { return sfz::centsFactor(c); });
    auto gainOfDb = cacheSameArgument([](float dB) { return db2mag(dB); });
    size_t differing = 0;
    for (float argument : arguments) {
        const float ratio = ratioOfCents(argument);
        const float expectedRatio = sfz::centsFactor(argument);
        const float gain = gainOfDb(argument * 0.01f);
        const float expectedGain = db2mag(argument * 0.01f);
        differing += std::memcmp(&ratio, &expectedRatio, sizeof(float)) == 0 ? 0 : 1;
        differing += std::memcmp(&gain, &expectedGain, sizeof(float)) == 0 ? 0 : 1;
    }
    AUD_CHECK(differing == 0);
}

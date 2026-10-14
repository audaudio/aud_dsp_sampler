// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The micro-benchmark of ticket 28: what the hot spots of an app's audio
// thread cost per call - the flush of the MIDI state, the compressor and
// the gain of the app's limiter on 180 frames, also in the two seconds
// after a loud tone, an idle synth and four synths of which three play
// dense chords. `node scripts/test-native.js --bench`
// builds it twice, at -O3 like the build hook, with the fork of a release
// before the change and with the fork of now, and prints the table. Only the
// public API of sfizz, so that both compile it.
//
// Usage: sfizz_bench <folder>
//
// Prints a line `AUD_BENCH {"name": ..., "ns": ...}` per measurement and
// writes the renders of the app's instrument into <folder>:
// render-golden.wav, the scenario of the golden, and render-bend.wav, the
// scenario with notes off their roots and the pitch bend.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "AudioSpan.h"
#include "Effects.h"
#include "MidiState.h"
#include "Opcode.h"
#include "ScopedFTZ.h"
#include "Synth.h"
#include "effects/Compressor.h"
#include "effects/Gain.h"

#include "sfizz_app_instrument.hpp"

namespace {

volatile float sink = 0;

// The nanoseconds of one call of `body`: the median of 9 batches of about
// 30 ms each, after a batch to warm up.
template <class F>
double nsPerCall(F&& body)
{
    using Clock = std::chrono::steady_clock;
    auto timeOf = [&](long calls) {
        const auto start = Clock::now();
        for (long i = 0; i < calls; ++i)
            body();
        return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    };
    long calls = 1;
    while (timeOf(calls) < 30e6 && calls < (1L << 30))
        calls *= 2;
    std::vector<double> batches;
    for (int b = 0; b < 9; ++b)
        batches.push_back(timeOf(calls) / static_cast<double>(calls));
    std::sort(batches.begin(), batches.end());
    return batches[batches.size() / 2];
}

void report(const char* name, double ns)
{
    std::printf("AUD_BENCH {\"name\": \"%s\", \"ns\": %.3f}\n", name, ns);
    std::fflush(stdout);
}

std::vector<sfz::Opcode> limiterOpcodes()
{
    return {
        { "type", "comp" }, { "comp_attack", "0" }, { "comp_release", "0.05" },
        { "comp_ratio", "50" }, { "comp_threshold", "-0.5" }, { "comp_stlink", "0" },
    };
}

// Compresses blocks of a stereo sine of `amplitude` with the app's limiter.
double compressorNs(float amplitude)
{
    constexpr int kFrames = sfizz_app::kBlockFrames;
    std::unique_ptr<sfz::Effect> effect = sfz::fx::Compressor::makeInstance(limiterOpcodes());
    effect->setSampleRate(sfizz_app::kSampleRate);
    effect->setSamplesPerBlock(kFrames);
    // 100 blocks of a tone of whole cycles, played in a loop.
    constexpr int kBlocks = 100;
    std::vector<float> signal[2];
    for (int c = 0; c < 2; ++c) {
        signal[c].resize(kBlocks * kFrames);
        for (int i = 0; i < kBlocks * kFrames; ++i)
            signal[c][i] = amplitude * static_cast<float>(std::sin(2 * M_PI * 240.0 * (i + c * 10) / sfizz_app::kSampleRate));
    }
    std::vector<float> out[2] = { std::vector<float>(kFrames), std::vector<float>(kFrames) };
    float* outputs[2] = { out[0].data(), out[1].data() };
    int block = 0;
    ScopedFTZ ftz;
    return nsPerCall([&] {
        const float* inputs[2] = { signal[0].data() + block * kFrames, signal[1].data() + block * kFrames };
        effect->process(inputs, outputs, kFrames);
        block = (block + 1) % kBlocks;
        sink = sink + out[0][7];
    });
}

// The blocks of the two seconds from a loud tone on: a tenth of a second of
// a stereo sine at twice full scale through the app's limiter, then
// silence, in which the limiter comes to rest; the mean per block.
double compressorAfterToneNs()
{
    constexpr int kFrames = sfizz_app::kBlockFrames;
    std::unique_ptr<sfz::Effect> effect = sfz::fx::Compressor::makeInstance(limiterOpcodes());
    effect->setSampleRate(sfizz_app::kSampleRate);
    effect->setSamplesPerBlock(kFrames);
    const int toneBlocks = static_cast<int>(0.1 * sfizz_app::kSampleRate) / kFrames;
    const int blocks = static_cast<int>(2.0 * sfizz_app::kSampleRate) / kFrames;
    std::vector<float> tone[2];
    for (int c = 0; c < 2; ++c) {
        tone[c].resize(toneBlocks * kFrames);
        for (int i = 0; i < toneBlocks * kFrames; ++i)
            tone[c][i] = 2.0f * static_cast<float>(std::sin(2 * M_PI * 240.0 * (i + c * 10) / sfizz_app::kSampleRate));
    }
    const std::vector<float> silence(kFrames, 0.0f);
    std::vector<float> out[2] = { std::vector<float>(kFrames), std::vector<float>(kFrames) };
    float* outputs[2] = { out[0].data(), out[1].data() };
    int block = 0;
    ScopedFTZ ftz;
    return nsPerCall([&] {
        const bool sounding = block < toneBlocks;
        const float* inputs[2] = {
            sounding ? tone[0].data() + block * kFrames : silence.data(),
            sounding ? tone[1].data() + block * kFrames : silence.data(),
        };
        effect->process(inputs, outputs, kFrames);
        block = (block + 1) % blocks;
        sink = sink + out[0][7];
    });
}

double gainNs()
{
    constexpr int kFrames = sfizz_app::kBlockFrames;
    const std::vector<sfz::Opcode> opcodes = { { "type", "gain" }, { "gain", "7.35" } };
    std::unique_ptr<sfz::Effect> effect = sfz::fx::Gain::makeInstance(opcodes);
    effect->setSampleRate(sfizz_app::kSampleRate);
    effect->setSamplesPerBlock(kFrames);
    std::vector<float> buffer[2] = { std::vector<float>(kFrames, 0.25f), std::vector<float>(kFrames, 0.25f) };
    float* channels[2] = { buffer[0].data(), buffer[1].data() };
    return nsPerCall([&] {
        effect->process(channels, channels, kFrames);
        // Keeps the samples from growing without bound.
        buffer[0][0] = 0.25f;
        sink = sink + buffer[1][5];
    });
}

// Renders blocks of `synths`, the first `playing` of which play a dense
// chord at the highest velocity every 250 ms - notes on and off their roots,
// as an app plays - and the others nothing.
double synthsNs(std::vector<std::unique_ptr<sfz::Synth>>& synths, int playing)
{
    constexpr int kFrames = sfizz_app::kBlockFrames;
    std::vector<float> left(kFrames);
    std::vector<float> right(kFrames);
    float* channels[2] = { left.data(), right.data() };
    const int chords[3][7] = {
        { 36, 43, 48, 52, 55, 59, 62 },
        { 41, 48, 53, 57, 60, 64, 67 },
        { 43, 50, 55, 59, 62, 65, 69 },
    };
    // 250 ms in blocks.
    constexpr int kStep = 67;
    long block = 0;
    return nsPerCall([&] {
        const int step = static_cast<int>(block / kStep);
        const bool start = block % kStep == 0;
        for (int s = 0; s < playing; ++s) {
            sfz::Synth& synth = *synths[s];
            if (start) {
                const int* previous = chords[(step + 2 + s) % 3];
                const int* next = chords[(step + s) % 3];
                for (int i = 0; i < 7; ++i)
                    synth.noteOff(i * 3, previous[i] + 12 * s, 0);
                for (int i = 0; i < 7; ++i)
                    synth.noteOn(i * 5, next[i] + 12 * s, 127);
            }
        }
        for (auto& synth : synths) {
            synth->renderBlock(sfz::AudioSpan<float>(channels, 2, 0, kFrames));
            sink = sink + left[3];
        }
        block += 1;
    });
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "Usage: sfizz_bench <folder>\n");
        return 2;
    }
    const std::string folder = argv[1];

    // The MIDI state at the end of every block of a synth.
    {
        sfz::MidiState state;
        state.setSamplesPerBlock(sfizz_app::kBlockFrames);
        report("flushEvents, 0 vectors with events", nsPerCall([&] { state.flushEvents(); }));
        float value = 0;
        report("flushEvents, 1 vector with events (+1 ccEvent)", nsPerCall([&] {
            state.ccEvent(17, 7, value);
            value = value < 1 ? value + 1e-3f : 0;
            state.flushEvents();
        }));
        report("flushEvents, 10 vectors with events (+10 ccEvent)", nsPerCall([&] {
            for (int k = 0; k < 10; ++k)
                state.ccEvent(10 + 7 * k, 20 + k, value);
            value = value < 1 ? value + 1e-3f : 0;
            state.flushEvents();
        }));
    }

    // The limiter of the app's instruments per block of 180 frames.
    report("compressor 180 frames, silent", compressorNs(0.0f));
    report("compressor 180 frames, below the threshold", compressorNs(0.3f));
    report("compressor 180 frames, over the threshold", compressorNs(2.0f));
    report("compressor 180 frames, the 2 s from a loud tone on", compressorAfterToneNs());
    report("gain effect 180 frames", gainNs());

    // Synths with the app's instrument.
    const std::string text = sfizz_app::writeInstrument(folder);
    if (text.empty()) {
        std::fprintf(stderr, "Could not write the instrument into %s\n", folder.c_str());
        return 1;
    }
    {
        std::vector<std::unique_ptr<sfz::Synth>> synths;
        for (int i = 0; i < 4; ++i) {
            synths.emplace_back(new sfz::Synth());
            if (!sfizz_app::load(*synths.back(), folder, text)) {
                std::fprintf(stderr, "Could not load the instrument\n");
                return 1;
            }
        }
        std::vector<std::unique_ptr<sfz::Synth>> idle;
        idle.push_back(std::move(synths.back()));
        synths.pop_back();
        report("idle synth 180 frames", synthsNs(idle, 0));
        synths.push_back(std::move(idle.front()));
        report("4 synths 180 frames, 3 play, 1 idle", synthsNs(synths, 3));
    }

    // The renders the script compares.
    const std::vector<float> golden = sfizz_app::render(folder, text, false, sfizz_app::kGoldenBlocks);
    const std::vector<float> bend = sfizz_app::render(folder, text, true, sfizz_app::kGoldenBlocks);
    if (golden.empty() || bend.empty() || !sfizz_app::writeWav(folder + "/render-golden.wav", golden, 2) ||
        !sfizz_app::writeWav(folder + "/render-bend.wav", bend, 2)) {
        std::fprintf(stderr, "Could not render into %s\n", folder.c_str());
        return 1;
    }
    return 0;
}

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The micro-benchmark of ticket 28: what the hot spots of an app's audio
// thread cost per call - the flush of the MIDI state, the compressor and
// the gain of the app's limiter on 180 frames, an idle synth and four synths
// of which three play dense chords. `node scripts/test-native.js --bench`
// builds it twice, at -O3 like the build hook, with the fork of a release
// before the change and with the fork of now, and prints the table. Only the
// public API of sfizz, so that both compile it.
//
// Ticket 30 adds the voice loop. A block of a synth that holds 1, 8 and 32
// notes, by the interpolation - sample_quality 1 (linear), 2 (Hermite, the
// default the app plays with) and 3 (the windowed sinc of 8 points) - and
// by the envelope - the ADSR of ampeg_* or the flex EG of the app's
// instruments -, on mono samples and on stereo ones, without and with the
// pitch bend moving in every block. A straight line through the three
// gives the cost of a voice (its slope) and the fixed part of a block
// (where it meets no voice). These variants leave out the app's limiter:
// its cost depends on the level, which the number of voices changes, and
// is measured on its own above. And the voice start: a block in which 10
// voices start at once, as the app's stress test does every 250 ms,
// against a block of the same voices without note-ons.
//
// Usage: sfizz_bench <folder>
//
// Prints a line `AUD_BENCH {"name": ..., "ns": ...}` per measurement and
// writes the renders of the app's instrument into <folder>:
// render-golden.wav, the scenario of the golden, and render-bend.wav, the
// scenario with notes off their roots and the pitch bend; and that
// scenario on every variant of the voice loop, render-<variant>.wav.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
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

using Clock = std::chrono::steady_clock;

double nsBetween(Clock::time_point start, Clock::time_point end)
{
    return std::chrono::duration<double, std::nano>(end - start).count();
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

// The nanoseconds of one call of `body`: the median of 9 batches of about
// 30 ms each, after a batch to warm up.
template <class F>
double nsPerCall(F&& body)
{
    auto timeOf = [&](long calls) {
        const auto start = Clock::now();
        for (long i = 0; i < calls; ++i)
            body();
        return nsBetween(start, Clock::now());
    };
    long calls = 1;
    while (timeOf(calls) < 30e6 && calls < (1L << 30))
        calls *= 2;
    std::vector<double> batches;
    for (int b = 0; b < 9; ++b)
        batches.push_back(timeOf(calls) / static_cast<double>(calls));
    return median(batches);
}

void report(const std::string& name, double ns)
{
    std::printf("AUD_BENCH {\"name\": \"%s\", \"ns\": %.3f}\n", name.c_str(), ns);
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

// ............................................................................
// The voice loop (ticket 30)

// Renders blocks of 180 frames of one synth.
class Blocks {
public:
    explicit Blocks(sfz::Synth& synth)
        : synth_(synth)
    {
    }

    void render()
    {
        float* channels[2] = { left_.data(), right_.data() };
        synth_.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, sfizz_app::kBlockFrames));
        sink = sink + left_[3];
    }

private:
    sfz::Synth& synth_;
    std::vector<float> left_ = std::vector<float>(sfizz_app::kBlockFrames);
    std::vector<float> right_ = std::vector<float>(sfizz_app::kBlockFrames);
};

// A variant of the app's instrument the voice loop is measured on.
struct Config {
    const char* name;
    // The variant in the name of its render.
    const char* file;
    int sampleQuality;
    bool flexEg;
    int channels;
};

// The interpolations of sfizz the app could choose, each with both
// envelopes on mono samples, and the app's own on stereo samples.
// Qualities 4 to 10 are longer windowed sincs; 0, the nearest sample, is
// too coarse for an instrument. No quality picks the B-spline: Voice.cpp
// has it in place of Hermite under `#if 0`.
const Config kConfigs[] = {
    { "linear, ADSR, mono", "linear-adsr-mono", 1, false, 1 },
    { "linear, flex EG, mono", "linear-flex-mono", 1, true, 1 },
    { "hermite, ADSR, mono", "hermite-adsr-mono", 2, false, 1 },
    { "hermite, flex EG, mono", "hermite-flex-mono", 2, true, 1 },
    { "sinc 8, ADSR, mono", "sinc8-adsr-mono", 3, false, 1 },
    { "sinc 8, flex EG, mono", "sinc8-flex-mono", 3, true, 1 },
    { "hermite, flex EG, stereo", "hermite-flex-stereo", 2, true, 2 },
};

// The voices a block is measured with; the straight line goes through
// them.
constexpr int kMostVoices = 32;
const int kVoiceCounts[] = { 1, 8, kMostVoices };

// The lowest key the voices hold, one key apart from there up: most of them
// off the roots of their samples, so that the interpolation steps between
// the samples' frames, as the app plays.
constexpr int kLowestKey = 40;

// The note-ons of a burst of the app's stress test.
constexpr int kBurst = 10;

// The instrument of `config`: the app's, with the ADSR envelope in place
// of the flex EG if so, the interpolation set by opcode, and the samples
// with the channels of `config`; without the limiter.
sfizz_app::Variant variantOf(const Config& config)
{
    const std::string envelope = config.flexEg ? sfizz_app::kAppGroup : "ampeg_attack=0.005 ampeg_release=0.3 ampeg_sustain=100";
    sfizz_app::Variant variant;
    variant.group = envelope + " sample_quality=" + std::to_string(config.sampleQuality);
    variant.channels = config.channels;
    variant.limiter = false;
    return variant;
}

// Loads the instrument of `config`, written into `folder`, into `synth`.
// Ends the benchmark if it cannot.
void loadConfig(sfz::Synth& synth, const std::string& folder, const Config& config)
{
    const std::string text = sfizz_app::writeInstrument(folder, variantOf(config));
    if (text.empty() || !sfizz_app::load(synth, folder, text)) {
        std::fprintf(stderr, "Could not load the instrument %s from %s\n", config.file, folder.c_str());
        std::exit(1);
    }
}

// Starts a note on each of `count` keys from `firstKey` up.
void noteOns(sfz::Synth& synth, int firstKey, int count)
{
    for (int i = 0; i < count; ++i)
        synth.noteOn(0, firstKey + i, 127);
}

// Ends the benchmark unless `synth` sounds `voices` voices: the
// measurement would not show what its name says.
void expectVoices(const sfz::Synth& synth, int voices)
{
    if (synth.getNumActiveVoices() == voices)
        return;
    std::fprintf(stderr, "The synth sounds %d voices instead of %d\n", synth.getNumActiveVoices(), voices);
    std::exit(1);
}

// The nanoseconds of a block of `synth` while it holds `voices` notes past
// their attack. `bend` moves the pitch bend to a new value at the end of
// every block, so that the pitch of every voice glides through the block.
double heldNs(sfz::Synth& synth, int voices, bool bend)
{
    Blocks blocks(synth);
    // Each count starts from silence and without a bend.
    synth.allSoundOff();
    synth.hdPitchWheel(0, 0.0f);
    noteOns(synth, kLowestKey, voices);
    for (int b = 0; b < 20; ++b)
        blocks.render();
    expectVoices(synth, voices);
    float phase = 0;
    return nsPerCall([&] {
        if (bend) {
            synth.hdPitchWheel(sfizz_app::kBlockFrames - 1, 0.5f * std::sin(phase));
            phase = phase < 6.2f ? phase + 0.1f : 0.0f;
        }
        blocks.render();
    });
}

// The fixed part and the part per voice of a block: the straight line
// through the measured blocks, by least squares.
struct Line {
    double perBlock;
    double perVoice;
};

Line lineThrough(const std::vector<double>& ns)
{
    const int n = static_cast<int>(ns.size());
    double meanVoices = 0;
    double meanNs = 0;
    for (int i = 0; i < n; ++i) {
        meanVoices += kVoiceCounts[i] / static_cast<double>(n);
        meanNs += ns[i] / n;
    }
    double covariance = 0;
    double variance = 0;
    for (int i = 0; i < n; ++i) {
        covariance += (kVoiceCounts[i] - meanVoices) * (ns[i] - meanNs);
        variance += (kVoiceCounts[i] - meanVoices) * (kVoiceCounts[i] - meanVoices);
    }
    const double perVoice = covariance / variance;
    return { meanNs - perVoice * meanVoices, perVoice };
}

// Measures the blocks of `config` with every count of voices; reports each
// and returns the straight line through them.
Line measureVoices(const std::string& folder, const Config& config, bool bend)
{
    sfz::Synth synth;
    loadConfig(synth, folder, config);
    const std::string label = std::string(config.name) + (bend ? ", bend" : "");
    std::vector<double> ns;
    for (int voices : kVoiceCounts) {
        ns.push_back(heldNs(synth, voices, bend));
        report(std::to_string(voices) + (voices == 1 ? " voice, " : " voices, ") + label, ns.back());
    }
    return lineThrough(ns);
}

// The nanoseconds of a block in which a burst of note-ons starts 10 voices,
// and of the block three blocks later, when the same voices sound without
// note-ons; `held` notes sound throughout. The medians of at least 51
// rounds and of 300 ms of rounds; each round starts from silence, outside
// the time measured. The extra of the burst is the median of the
// differences within a round, which a clock that changes from round to
// round moves less than the difference of the medians.
struct Burst {
    double withNoteOns;
    double without;
    double extra;
};

Burst burstNs(sfz::Synth& synth, int held)
{
    Blocks blocks(synth);
    // The burst plays the keys above those of the most voices held.
    const int burstKey = kLowestKey + kMostVoices - kBurst;
    std::vector<double> withNoteOns;
    std::vector<double> without;
    std::vector<double> extra;
    const auto start = Clock::now();
    for (int round = 0; round < 5000; ++round) {
        if (round >= 51 && nsBetween(start, Clock::now()) > 300e6)
            break;
        synth.allSoundOff();
        noteOns(synth, kLowestKey, held);
        for (int b = 0; b < 4; ++b)
            blocks.render();
        const auto beforeBurst = Clock::now();
        noteOns(synth, burstKey, kBurst);
        blocks.render();
        const auto afterBurst = Clock::now();
        for (int b = 0; b < 2; ++b)
            blocks.render();
        const auto beforeQuiet = Clock::now();
        blocks.render();
        const auto afterQuiet = Clock::now();
        withNoteOns.push_back(nsBetween(beforeBurst, afterBurst));
        without.push_back(nsBetween(beforeQuiet, afterQuiet));
        extra.push_back(withNoteOns.back() - without.back());
    }
    expectVoices(synth, held + kBurst);
    return { median(withNoteOns), median(without), median(extra) };
}

// Measures and reports the burst on the instrument of `config`.
void measureBurst(const std::string& folder, const Config& config, int held)
{
    sfz::Synth synth;
    loadConfig(synth, folder, config);
    const Burst burst = burstNs(synth, held);
    const std::string label = std::to_string(kBurst) + " note-ons" +
        (held > 0 ? " over " + std::to_string(held) + " held, " : ", ") + config.name;
    report(label + ": their block", burst.withNoteOns);
    report(label + ": a block without", burst.without);
    report(label + ": per note-on", burst.extra / kBurst);
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

    // The voice loop, every variant without the pitch bend, and with it
    // those with the app's flex EG: the bend changes the interpolation, not
    // the envelope.
    std::vector<std::pair<std::string, Line>> lines;
    for (const Config& config : kConfigs) {
        lines.emplace_back(config.name, measureVoices(folder, config, false));
        if (config.flexEg)
            lines.emplace_back(std::string(config.name) + ", bend", measureVoices(folder, config, true));
    }

    // The voice start, on the app's interpolation with both envelopes.
    const Config& hermiteFlex = kConfigs[3];
    const Config& hermiteAdsr = kConfigs[2];
    measureBurst(folder, hermiteFlex, 0);
    measureBurst(folder, hermiteFlex, kMostVoices - kBurst);
    measureBurst(folder, hermiteAdsr, 0);

    // One number per variant.
    for (const auto& entry : lines)
        report("per voice, " + entry.first, entry.second.perVoice);
    for (const auto& entry : lines)
        report("per block, " + entry.first, entry.second.perBlock);

    // The renders the script compares.
    const std::vector<float> golden = sfizz_app::render(folder, text, false, sfizz_app::kGoldenBlocks);
    const std::vector<float> bend = sfizz_app::render(folder, text, true, sfizz_app::kGoldenBlocks);
    if (golden.empty() || bend.empty() || !sfizz_app::writeWav(folder + "/render-golden.wav", golden, 2) ||
        !sfizz_app::writeWav(folder + "/render-bend.wav", bend, 2)) {
        std::fprintf(stderr, "Could not render into %s\n", folder.c_str());
        return 1;
    }
    for (const Config& config : kConfigs) {
        const std::string variant = sfizz_app::writeInstrument(folder, variantOf(config));
        const std::vector<float> render = sfizz_app::render(folder, variant, true, sfizz_app::kGoldenBlocks);
        if (render.empty() || !sfizz_app::writeWav(folder + "/render-" + config.file + ".wav", render, 2)) {
            std::fprintf(stderr, "Could not render %s into %s\n", config.file, folder.c_str());
            return 1;
        }
    }
    return 0;
}

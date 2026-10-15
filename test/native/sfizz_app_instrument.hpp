// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// An instrument like those of an app that plays the sampler, and what it
// plays (ticket 28): sine samples on roots six keys apart, a flex EG for the
// amplitude (eg1_ampeg=1), the volume on CC 7 through a curve with
// smoothing, and on the main bus the app's limiter - a compressor without
// attack at -0.5 dB and 50:1 - and a gain of 7.35 dB. The native test of
// the render compares it with the golden the original code rendered, and
// the benchmark of scripts/test-native.js --bench renders it with the code
// of a release before the change and with the code of now. Only the public
// API of sfizz, so that both compile it. The benchmark of ticket 30 also
// plays variants of it: other opcodes in the group, which choose the
// envelope and the interpolation, and stereo samples.

#ifndef SFIZZ_APP_INSTRUMENT_HPP
#define SFIZZ_APP_INSTRUMENT_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "AudioSpan.h"
#include "Synth.h"

namespace sfizz_app {

constexpr int kSampleRate = 48000;
// The frames of an app's callback on an Android device.
constexpr int kBlockFrames = 180;
// The roots of the samples; a region plays its root and the keys up to
// three below and two above it, as the app's instruments do.
const int kRoots[] = { 36, 42, 48, 54, 60, 66, 72, 78, 84 };

// Writes a WAV file of 32-bit floats, `channels` interleaved.
inline bool writeWav(const std::string& file, const std::vector<float>& samples, int channels)
{
    std::ofstream out(file, std::ios::binary);
    if (!out)
        return false;
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    auto u32 = [&out](uint32_t value) { out.write(reinterpret_cast<const char*>(&value), 4); };
    auto u16 = [&out](uint16_t value) { out.write(reinterpret_cast<const char*>(&value), 2); };
    out.write("RIFF", 4);
    u32(36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16);
    u16(3); // IEEE float
    u16(static_cast<uint16_t>(channels));
    u32(kSampleRate);
    u32(kSampleRate * channels * 4);
    u16(static_cast<uint16_t>(channels * 4));
    u16(32);
    out.write("data", 4);
    u32(dataBytes);
    out.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    return static_cast<bool>(out);
}

// The samples of a WAV file of 32-bit floats, or none.
inline std::vector<float> readWav(const std::string& file)
{
    std::ifstream in(file, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (size_t at = 12; at + 8 <= bytes.size();) {
        uint32_t size;
        std::memcpy(&size, &bytes[at + 4], 4);
        if (std::memcmp(&bytes[at], "data", 4) == 0 && at + 8 + size <= bytes.size()) {
            std::vector<float> samples(size / sizeof(float));
            std::memcpy(samples.data(), &bytes[at + 8], samples.size() * sizeof(float));
            return samples;
        }
        at += 8 + size + (size & 1);
    }
    return {};
}

// The opcodes of the group of the app's instruments: the flex EG for the
// amplitude, with the ADSR envelope beside it that eg1_ampeg=1 replaces.
const char* const kAppGroup =
    "ampeg_attack=0.005 ampeg_release=0.3 ampeg_sustain=100 eg1_ampeg=1 "
    "eg1_sustain=1 eg1_time1=0.005 eg1_level1=1 eg1_shape1=0 eg1_time2=0.3 "
    "eg1_level2=0 eg1_shape2=0";

// What a variant of the instrument changes: the opcodes of its group, the
// channels of its samples, and whether the main bus has the app's limiter.
// The default is the app's instrument.
struct Variant {
    std::string group = kAppGroup;
    int channels = 1;
    bool limiter = true;
};

// Writes one second of a sine on every root into `directory` and returns
// the SFZ text that plays them. A sine of whole cycles per second loops
// without a click. Stereo samples hold the sine a quarter cycle ahead on
// the right, in files of their own.
inline std::string writeInstrument(const std::string& directory, const Variant& variant = Variant())
{
    std::string text =
        "<control>\n"
        "set_hdcc7=0.7\n"
        "<curve> curve_index=7 v000=-128 v010=-40 v017=-30 v029=-20 v038=-15 v051=-10 "
        "v064=-6 v076=-3 v089=0 v108=3 v127=6\n";
    if (variant.limiter) {
        text +=
            "<effect> bus=main type=comp comp_attack=0 comp_release=0.05 comp_ratio=50 "
            "comp_threshold=-0.5 comp_stlink=0\n"
            "<effect> bus=main type=gain gain=7.35\n";
    }
    text +=
        "<global> global_volume=-2 volume_oncc7=1 volume_curvecc7=7 volume_smoothcc7=10\n"
        "<group> " + variant.group + "\n";
    const int channels = variant.channels;
    for (int root : kRoots) {
        const double frequency = std::round(440.0 * std::pow(2.0, (root - 69) / 12.0));
        std::vector<float> sine(static_cast<size_t>(kSampleRate) * channels);
        for (int i = 0; i < kSampleRate; ++i) {
            for (int c = 0; c < channels; ++c)
                sine[i * channels + c] =
                    static_cast<float>(0.5 * std::sin(2 * M_PI * frequency * i / kSampleRate + c * M_PI / 2));
        }
        const std::string name =
            "sine" + std::to_string(root) + (channels == 1 ? "" : "-" + std::to_string(channels)) + ".wav";
        if (!writeWav(directory + "/" + name, sine, channels))
            return "";
        text += "<region> sample=" + name + " lokey=" + std::to_string(root - 3) +
            " hikey=" + std::to_string(root + 2) + " pitch_keycenter=" + std::to_string(root) +
            " loop_mode=loop_continuous loop_start=0 loop_end=" + std::to_string(kSampleRate - 1) + "\n";
    }
    return text;
}

struct Event {
    enum Type { NoteOn, NoteOff, Controller, PitchBend, ChannelPressure, PolyPressure };
    int block;
    int delay;
    Type type;
    int number;
    float value;
};

// What the instrument plays, block by block: silence, a dense chord at the
// highest velocity that the limiter holds, moves of CC 7, pressure, the
// release, and a quiet note below the threshold. `bend` adds what a golden
// of one platform cannot hold for another, where pow and exp2 may round
// differently and the position of a voice drifts: notes off their roots
// and the pitch bend.
inline std::vector<Event> scenario(bool bend)
{
    std::vector<Event> events;
    const int chord[] = { 36, 48, 54, 60, 66, 72, 78 };
    const int delays[] = { 0, 13, 27, 41, 55, 90, 179 };
    for (int i = 0; i < 7; ++i)
        events.push_back({ 10, delays[i], Event::NoteOn, chord[i], 127 });
    if (bend) {
        for (int note : { 50, 57, 63, 69 })
            events.push_back({ 12, note % 180, Event::NoteOn, note, 127 });
        for (int block = 20; block < 50; block += 3)
            events.push_back({ block, (block * 37) % 180, Event::PitchBend, 0, std::sin(block * 0.3f) });
        events.push_back({ 50, 0, Event::PitchBend, 0, 0.0f });
    }
    events.push_back({ 25, 50, Event::Controller, 7, 100.0f / 127 });
    events.push_back({ 40, 0, Event::Controller, 7, 0.5f });
    events.push_back({ 40, 120, Event::Controller, 1, 0.25f });
    events.push_back({ 55, 7, Event::ChannelPressure, 0, 0.5f });
    events.push_back({ 55, 9, Event::PolyPressure, 60, 0.75f });
    for (int i = 0; i < 7; ++i)
        events.push_back({ 60, delays[i] / 2, Event::NoteOff, chord[i], 0 });
    if (bend) {
        for (int note : { 50, 57, 63, 69 })
            events.push_back({ 62, note, Event::NoteOff, note, 0 });
    }
    events.push_back({ 100, 77, Event::NoteOn, 60, 20 });
    events.push_back({ 120, 3, Event::Controller, 7, 0.7f });
    events.push_back({ 130, 0, Event::NoteOff, 60, 0 });
    return events;
}

// Sends the events of `block` to `synth`.
inline void dispatch(sfz::Synth& synth, const std::vector<Event>& events, int block)
{
    for (const Event& event : events) {
        if (event.block != block)
            continue;
        switch (event.type) {
        case Event::NoteOn:
            synth.noteOn(event.delay, event.number, static_cast<int>(event.value));
            break;
        case Event::NoteOff:
            synth.noteOff(event.delay, event.number, 0);
            break;
        case Event::Controller:
            synth.hdcc(event.delay, event.number, event.value);
            break;
        case Event::PitchBend:
            synth.hdPitchWheel(event.delay, event.value);
            break;
        case Event::ChannelPressure:
            synth.hdChannelAftertouch(event.delay, event.value);
            break;
        case Event::PolyPressure:
            synth.hdPolyAftertouch(event.delay, event.number, event.value);
            break;
        }
    }
}

// Configures `synth` as the node does for an app and loads the instrument.
inline bool load(sfz::Synth& synth, const std::string& directory, const std::string& text)
{
    synth.setSampleRate(kSampleRate);
    synth.setSamplesPerBlock(kBlockFrames);
    return synth.loadSfzString(directory + "/app.sfz", text);
}

// Renders `blocks` blocks of the scenario; returns the frames, interleaved.
inline std::vector<float> render(const std::string& directory, const std::string& text, bool bend, int blocks)
{
    sfz::Synth synth;
    if (!load(synth, directory, text))
        return {};
    const std::vector<Event> events = scenario(bend);
    std::vector<float> left(kBlockFrames);
    std::vector<float> right(kBlockFrames);
    float* channels[2] = { left.data(), right.data() };
    std::vector<float> result;
    result.reserve(static_cast<size_t>(blocks) * kBlockFrames * 2);
    for (int block = 0; block < blocks; ++block) {
        dispatch(synth, events, block);
        synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, kBlockFrames));
        for (int i = 0; i < kBlockFrames; ++i) {
            result.push_back(left[i]);
            result.push_back(right[i]);
        }
    }
    return result;
}

// The blocks of the render the golden holds.
constexpr int kGoldenBlocks = 150;

} // namespace sfizz_app

#endif // SFIZZ_APP_INSTRUMENT_HPP

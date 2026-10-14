// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the MIDI state of the sfizz fork (src/sfizz/MidiState.cpp): its
// flushEvents() flushes only the event vectors that received events since
// the last flush (ticket 28), where the original flushed all 642 of them in
// every block. Random streams of events, resets and flushes drive the state
// and a reference that keeps the original algorithm; after every block
// every event vector, every value a voice asks for and the modulations the
// controllers build from the events are the same, bit for bit. A controller
// number out of range changes nothing (release 0.3.2).

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "MidiState.h"
#include "ModifierHelpers.h"
#include "SfzHelpers.h"

#include "aud_test.hpp"

namespace {

// The event vectors of the original MidiState with its algorithm: every
// event inserted at its delay, every vector flushed in every block.
class OriginalEvents {
public:
    OriginalEvents() { resetEventStates(); }

    void ccEvent(int delay, int cc, float value) { insert(cc_[cc], delay, value); }
    void pitchBendEvent(int delay, float value) { insert(pitch_, delay, value); }
    void channelAftertouchEvent(int delay, float value) { insert(channelAftertouch_, delay, value); }
    void polyAftertouchEvent(int delay, int note, float value)
    {
        if (note < 0 || note >= 128)
            return;
        insert(polyAftertouch_[note], delay, value);
    }

    // The original flushEvents(), every vector.
    void flushEvents()
    {
        auto flush = [](sfz::EventVector& events) {
            events.front().value = events.back().value;
            events.front().delay = 0;
            events.resize(1);
        };
        for (auto& events : cc_)
            flush(events);
        for (auto& events : polyAftertouch_)
            flush(events);
        flush(pitch_);
        flush(channelAftertouch_);
    }

    void resetEventStates()
    {
        auto clear = [](sfz::EventVector& events) {
            events.clear();
            events.push_back({ 0, 0.0f });
        };
        for (auto& events : cc_)
            clear(events);
        for (auto& events : polyAftertouch_)
            clear(events);
        clear(pitch_);
        clear(channelAftertouch_);
    }

    // The events of the original resetNoteStates().
    void resetNoteStates()
    {
        const int resetCCs[] = { sfz::ExtendedCCs::noteOnVelocity, sfz::ExtendedCCs::keyboardNoteNumber,
            sfz::ExtendedCCs::unipolarRandom, sfz::ExtendedCCs::bipolarRandom,
            sfz::ExtendedCCs::keyboardNoteGate, sfz::ExtendedCCs::alternate };
        for (int cc : resetCCs) {
            cc_[cc].clear();
            cc_[cc].push_back({ 0, 0.0f });
        }
    }

    const sfz::EventVector& cc(int cc) const { return cc_[cc]; }
    const sfz::EventVector& polyAftertouch(int note) const { return polyAftertouch_[note]; }
    const sfz::EventVector& pitch() const { return pitch_; }
    const sfz::EventVector& channelAftertouch() const { return channelAftertouch_; }

private:
    static void insert(sfz::EventVector& events, int delay, float value)
    {
        const auto at = absl::c_lower_bound(events, delay, sfz::MidiEventDelayComparator {});
        if (at == events.end() || at->delay != delay)
            events.insert(at, { delay, value });
        else
            at->value = value;
    }

    std::array<sfz::EventVector, sfz::config::numCCs> cc_;
    std::array<sfz::EventVector, 128> polyAftertouch_;
    sfz::EventVector pitch_;
    sfz::EventVector channelAftertouch_;
};

bool sameBits(float a, float b)
{
    return std::memcmp(&a, &b, sizeof(float)) == 0;
}

bool sameEvents(const sfz::EventVector& a, const sfz::EventVector& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].delay != b[i].delay || !sameBits(a[i].value, b[i].value))
            return false;
    }
    return true;
}

// Whether every vector of `state` equals the one of `original`; counts the
// vectors that differ.
int differingVectors(const sfz::MidiState& state, const OriginalEvents& original)
{
    int differing = 0;
    for (int cc = 0; cc < sfz::config::numCCs; ++cc)
        differing += sameEvents(state.getCCEvents(cc), original.cc(cc)) ? 0 : 1;
    for (int note = 0; note < 128; ++note)
        differing += sameEvents(state.getPolyAftertouchEvents(note), original.polyAftertouch(note)) ? 0 : 1;
    differing += sameEvents(state.getPitchEvents(), original.pitch()) ? 0 : 1;
    differing += sameEvents(state.getChannelAftertouchEvents(), original.channelAftertouch()) ? 0 : 1;
    return differing;
}

// The value at `delay` of a vector, as getCCValueAt() reads it.
float valueAt(const sfz::EventVector& events, int delay)
{
    const auto at = absl::c_lower_bound(events, delay, sfz::MidiEventDelayComparator {});
    return at != events.end() ? at->value : events.back().value;
}

// Whether the values a voice asks for during a block are the same: the last
// value and the value at a delay of every vector, and the envelopes the
// modulation of a controller, of the pitch bend and of the aftertouch build
// from the events of the block.
bool sameQueries(const sfz::MidiState& state, const OriginalEvents& original, int blockSize, std::mt19937& random)
{
    bool same = true;
    std::uniform_int_distribution<int> delays(0, blockSize);
    for (int cc = 0; cc < sfz::config::numCCs; ++cc) {
        same &= sameBits(state.getCCValue(cc), original.cc(cc).back().value);
        const int delay = delays(random);
        same &= sameBits(state.getCCValueAt(cc, delay), valueAt(original.cc(cc), delay));
    }
    for (int note = 0; note < 128; ++note)
        same &= sameBits(state.getPolyAftertouch(note), original.polyAftertouch(note).back().value);
    same &= sameBits(state.getPitchBend(), original.pitch().back().value);
    same &= sameBits(state.getChannelAftertouch(), original.channelAftertouch().back().value);

    std::vector<float> envelope(blockSize);
    std::vector<float> expected(blockSize);
    auto sameEnvelope = [&](const sfz::EventVector& events, const sfz::EventVector& originalEvents) {
        const auto identity = [](float x) { return x; };
        sfz::linearEnvelope(events, absl::MakeSpan(envelope), identity);
        sfz::linearEnvelope(originalEvents, absl::MakeSpan(expected), identity);
        return std::memcmp(envelope.data(), expected.data(), sizeof(float) * envelope.size()) == 0;
    };
    const int modulatedCCs[] = { 1, 7, 11, 64, sfz::ExtendedCCs::noteOnVelocity, sfz::ExtendedCCs::unipolarRandom, 300 };
    for (int cc : modulatedCCs)
        same &= sameEnvelope(state.getCCEvents(cc), original.cc(cc));
    same &= sameEnvelope(state.getPitchEvents(), original.pitch());
    same &= sameEnvelope(state.getChannelAftertouchEvents(), original.channelAftertouch());
    same &= sameEnvelope(state.getPolyAftertouchEvents(60), original.polyAftertouch(60));
    return same;
}

// The CCs a note on writes, after the state wrote them: the reference
// takes the values the state drew, its random ones among them.
const int kNoteOnCCs[] = {
    sfz::ExtendedCCs::noteOnVelocity, sfz::ExtendedCCs::keyboardNoteNumber,
    sfz::ExtendedCCs::unipolarRandom, sfz::ExtendedCCs::bipolarRandom,
    sfz::ExtendedCCs::keyboardNoteGate, sfz::AriaExtendedCCs::keydelta,
    sfz::AriaExtendedCCs::absoluteKeydelta, sfz::ExtendedCCs::alternate,
};
const int kNoteOffCCs[] = {
    sfz::ExtendedCCs::noteOffVelocity, sfz::ExtendedCCs::keyboardNoteNumber,
    sfz::ExtendedCCs::unipolarRandom, sfz::ExtendedCCs::bipolarRandom,
};

template <size_t N>
void copyWritten(const sfz::MidiState& state, OriginalEvents& original, int delay, const int (&ccs)[N])
{
    for (int cc : ccs)
        original.ccEvent(delay, cc, valueAt(state.getCCEvents(cc), delay));
}

} // namespace

// ############################################################################
// Flushes of the vectors that received events

AUD_TEST(sfizz_midi_state_flushes_like_the_original_on_random_streams) {
    std::mt19937 random(28);
    sfz::MidiState state;
    OriginalEvents original;
    constexpr int kBlockSize = 180;
    state.setSamplesPerBlock(kBlockSize);
    AUD_CHECK(differingVectors(state, original) == 0);

    std::uniform_int_distribution<int> operations(0, 99);
    std::uniform_int_distribution<int> delays(0, kBlockSize - 1);
    std::uniform_int_distribution<int> anyCC(0, sfz::config::numCCs - 1);
    std::uniform_int_distribution<int> fewCCs(0, 7);
    std::uniform_int_distribution<int> notes(0, 127);
    std::uniform_int_distribution<int> polyNotes(-2, 129);
    std::uniform_int_distribution<int> blockLengths(0, 24);
    std::uniform_real_distribution<float> unipolar(0.0f, 1.0f);
    std::uniform_real_distribution<float> bipolar(-1.0f, 1.0f);
    const int commonCCs[] = { 1, 7, 10, 11, 64, 66, 74, 300 };

    constexpr int kBlocks = 3000;
    int events = 0;
    int differingBlocks = 0;
    int differingQueries = 0;
    for (int block = 0; block < kBlocks; ++block) {
        const int length = blockLengths(random);
        for (int i = 0; i < length; ++i) {
            const int operation = operations(random);
            const int delay = delays(random);
            if (operation < 35) {
                // Most CCs come again and again, some are rare.
                const int cc = operation < 25 ? commonCCs[fewCCs(random)] : anyCC(random);
                const float value = unipolar(random);
                state.ccEvent(delay, cc, value);
                original.ccEvent(delay, cc, value);
            } else if (operation < 45) {
                const float value = bipolar(random);
                state.pitchBendEvent(delay, value);
                original.pitchBendEvent(delay, value);
            } else if (operation < 52) {
                const float value = unipolar(random);
                state.channelAftertouchEvent(delay, value);
                original.channelAftertouchEvent(delay, value);
            } else if (operation < 62) {
                // Notes out of range are ignored by both.
                const int note = polyNotes(random);
                const float value = unipolar(random);
                state.polyAftertouchEvent(delay, note, value);
                original.polyAftertouchEvent(delay, note, value);
            } else if (operation < 78) {
                state.noteOnEvent(delay, notes(random), unipolar(random));
                copyWritten(state, original, delay, kNoteOnCCs);
            } else if (operation < 92) {
                state.noteOffEvent(delay, notes(random), unipolar(random));
                copyWritten(state, original, delay, kNoteOffCCs);
            } else if (operation < 94) {
                state.allNotesOff(delay);
                copyWritten(state, original, delay, kNoteOffCCs);
            } else if (operation < 96) {
                state.resetNoteStates();
                original.resetNoteStates();
            } else if (operation < 97) {
                state.resetEventStates();
                original.resetEventStates();
            } else {
                // A flush within the block, as Synth::clear() and a moved
                // playhead do.
                state.flushEvents();
                original.flushEvents();
            }
            events += 1;
        }
        // What the voices see during the block.
        if (!sameQueries(state, original, kBlockSize, random))
            differingQueries += 1;
        if (differingVectors(state, original) != 0)
            differingBlocks += 1;
        // The end of the block.
        state.advanceTime(kBlockSize);
        original.flushEvents();
        if (differingVectors(state, original) != 0)
            differingBlocks += 1;
    }
    AUD_CHECK(events > kBlocks * 5);
    AUD_CHECK(differingBlocks == 0);
    AUD_CHECK(differingQueries == 0);
}

AUD_TEST(sfizz_midi_state_flushes_every_vector_an_event_reached) {
    // Each kind of vector, one event late in the block: the flush moves its
    // value to delay 0 and drops the others.
    sfz::MidiState state;
    state.setSamplesPerBlock(256);
    state.ccEvent(100, 7, 0.25f);
    state.ccEvent(200, 7, 0.5f);
    state.ccEvent(10, 511, 1.0f);
    state.pitchBendEvent(50, -0.5f);
    state.channelAftertouchEvent(60, 0.75f);
    state.polyAftertouchEvent(70, 127, 0.125f);
    state.polyAftertouchEvent(70, 128, 0.125f); // out of range: ignored
    state.flushEvents();
    auto single = [](const sfz::EventVector& events, float value) {
        return events.size() == 1 && events[0].delay == 0 && events[0].value == value;
    };
    AUD_CHECK(single(state.getCCEvents(7), 0.5f));
    AUD_CHECK(single(state.getCCEvents(511), 1.0f));
    AUD_CHECK(single(state.getPitchEvents(), -0.5f));
    AUD_CHECK(single(state.getChannelAftertouchEvents(), 0.75f));
    AUD_CHECK(single(state.getPolyAftertouchEvents(127), 0.125f));
    AUD_CHECK(single(state.getCCEvents(8), 0.0f));
    AUD_CHECK(single(state.getPolyAftertouchEvents(126), 0.0f));
    // A reset in the middle of a block leaves nothing to flush, and the
    // vectors it cleared take new events.
    state.ccEvent(30, 7, 0.75f);
    state.resetEventStates();
    state.ccEvent(40, 7, 1.0f);
    state.flushEvents();
    AUD_CHECK(single(state.getCCEvents(7), 1.0f));
    state.flushEvents();
    AUD_CHECK(single(state.getCCEvents(7), 1.0f));
}

// ############################################################################
// Controller numbers out of range

AUD_TEST(sfizz_midi_state_ignores_a_cc_number_out_of_range) {
    // Numbers below 0 and from 512 on, e.g. of a host or a message, change
    // no event vector and leave nothing to flush; the event in range beside
    // them stays and flushes.
    sfz::MidiState state;
    state.setSamplesPerBlock(256);
    OriginalEvents original;
    state.ccEvent(10, 511, 0.5f);
    original.ccEvent(10, 511, 0.5f);
    const int outOfRange[] = { -1, sfz::config::numCCs, sfz::config::numCCs + 1,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max() };
    for (int cc : outOfRange)
        state.ccEvent(20, cc, 0.75f);
    AUD_CHECK(differingVectors(state, original) == 0);
    state.flushEvents();
    original.flushEvents();
    AUD_CHECK(differingVectors(state, original) == 0);
    for (int cc : outOfRange)
        state.ccEvent(0, cc, 1.0f);
    state.flushEvents();
    AUD_CHECK(differingVectors(state, original) == 0);
}

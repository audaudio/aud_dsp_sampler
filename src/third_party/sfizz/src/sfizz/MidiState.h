// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 28 of aud_audio_pm):
// flushEvents() flushes only the event vectors that received an event since
// the last flush, which a list of fixed size names; it flushed all 642 of
// them in every block, a third of the sampler's time on an app's audio
// thread. A vector no event reached holds its one event at delay 0 already,
// which a flush keeps as it is.

#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include "CCMap.h"
#include "Range.h"

namespace sfz
{
/**
 * @brief Holds the current "MIDI state", meaning the known state of all CCs
 * currently, as well as the note velocities that triggered the currently
 * pressed notes.
 *
 */
class MidiState
{
public:
    MidiState();

    /**
     * @brief Update the state after a note on event
     *
     * @param delay
     * @param noteNumber
     * @param velocity
     */
    void noteOnEvent(int delay, int noteNumber, float velocity) noexcept;

    /**
     * @brief Update the state after a note off event
     *
     * @param delay
     * @param noteNumber
     * @param velocity
     */
    void noteOffEvent(int delay, int noteNumber, float velocity) noexcept;

    /**
     * @brief Set all notes off
     *
     * @param delay
     */
    void allNotesOff(int delay) noexcept;

    /**
     * @brief Get the number of active notes
     */
    int getActiveNotes() const noexcept { return activeNotes; }

    /**
     * @brief Get the note duration since note on
     *
     * @param noteNumber
     * @param delay
     * @return float
     */
    float getNoteDuration(int noteNumber, int delay = 0) const;

    /**
     * @brief Set the maximum size of the blocks for the callback. The actual
     * size can be lower in each callback but should not be larger
     * than this value.
     *
     * @param samplesPerBlock
     */
    void setSamplesPerBlock(int samplesPerBlock) noexcept;
    /**
     * @brief Set the sample rate. If you do not call it it is initialized
     * to sfz::config::defaultSampleRate.
     *
     * @param sampleRate
     */
    void setSampleRate(float sampleRate) noexcept;
    /**
     * @brief Get the note on velocity for a given note
     *
     * @param noteNumber
     * @return float
     */
    float getNoteVelocity(int noteNumber) const noexcept;

    /**
     * @brief Get the velocity override value (sw_vel in SFZ)
     *
     * @return float
     */
    float getVelocityOverride() const noexcept;

    /**
     * @brief Register a pitch bend event
     *
     * @param pitchBendValue
     */
    void pitchBendEvent(int delay, float pitchBendValue) noexcept;

    /**
     * @brief Get the pitch bend status

     * @return int
     */
    float getPitchBend() const noexcept;

    /**
     * @brief Register a channel aftertouch event
     *
     * @param aftertouch
     */
    void channelAftertouchEvent(int delay, float aftertouch) noexcept;

    /**
     * @brief Register a channel aftertouch event
     *
     * @param aftertouch
     */
    void polyAftertouchEvent(int delay, int noteNumber, float aftertouch) noexcept;

    /**
     * @brief Get the channel aftertouch status

     * @return int
     */
    float getChannelAftertouch() const noexcept;

    /**
     * @brief Get the polyphonic aftertouch status

     * @return int
     */
    float getPolyAftertouch(int noteNumber) const noexcept;

    /**
     * @brief Get the current midi program
     *
     * @return int
     */
    int getProgram() const noexcept;
    /**
     * @brief Register a program change event
     *
     * @param delay
     * @param program
     */
    void programChangeEvent(int delay, int program) noexcept;

    /**
     * @brief Register a CC event
     *
     * @param ccNumber
     * @param ccValue
     */
    void ccEvent(int delay, int ccNumber, float ccValue) noexcept;

    /**
     * @brief Advances the internal clock of a given amount of samples.
     * You should call this at each callback. This will flush the events
     * in the midistate memory by calling flushEvents().
     *
     * @param numSamples the number of samples of clock advance
     */
    void advanceTime(int numSamples) noexcept;

    /**
     * @brief Returns current internal sample clock
     *
     */
    unsigned getInternalClock() const noexcept { return internalClock; }

    /**
     * @brief Flush events in all states, keeping only the last one as the "base" state
     *
     */
    void flushEvents() noexcept;

    /**
     * @brief Check if a note is currently depressed
     *
     * @param noteNumber
     * @return true
     * @return false
     */
    bool isNotePressed(int noteNumber) const noexcept { return noteStates[noteNumber]; }

    /**
     * @brief Get the last CC value for CC number
     *
     * @param ccNumber
     * @return float
     */
    float getCCValue(int ccNumber) const noexcept;

    /**
     * @brief Get the CC value for CC number
     *
     * @param ccNumber
     * @param delay
     * @return float
     */
    float getCCValueAt(int ccNumber, int delay) const noexcept;

    /**
     * @brief Reset the midi note states
     *
     */
    void resetNoteStates() noexcept;

    const EventVector& getCCEvents(int ccIdx) const noexcept;
    const EventVector& getPolyAftertouchEvents(int noteNumber) const noexcept;
    const EventVector& getPitchEvents() const noexcept;
    const EventVector& getChannelAftertouchEvents() const noexcept;
    /**
     * @brief Reset the midi event states (CC, AT, and pitch bend)
     *
     */
    void resetEventStates() noexcept;

private:

    /**
     * @brief Insert events in a sorted event vector.
     *
     * @param events
     * @param delay
     * @param value
     */
    void insertEventInVector(EventVector& events, int delay, float value);

    /**
     * @brief The slots of the event vectors in the list of those to flush:
     * the CCs, then the polyphonic aftertouch of each note, the pitch bend
     * and the channel aftertouch (Audanika, ticket 28).
     */
    static constexpr int polyAftertouchSlot { config::numCCs };
    static constexpr int pitchSlot { polyAftertouchSlot + 128 };
    static constexpr int channelAftertouchSlot { pitchSlot + 1 };
    static constexpr int numEventSlots { channelAftertouchSlot + 1 };

    /**
     * @brief Notes that the event vector of a slot received an event, so that
     * the next flush reaches it.
     */
    void markForFlush(int slot) noexcept
    {
        if (toFlush[slot])
            return;
        toFlush[slot] = true;
        slotsToFlush[numSlotsToFlush++] = static_cast<uint16_t>(slot);
    }

    /**
     * @brief The event vector of a slot.
     */
    EventVector& eventsOfSlot(int slot) noexcept;

    int activeNotes { 0 };

    /**
     * @brief Stores the note on times.
     *
     */
    MidiNoteArray<unsigned> noteOnTimes { {} };

    /**
     * @brief Stores the note off times.
     *
     */

    MidiNoteArray<unsigned> noteOffTimes { {} };

    /**
     * @brief Store the note states
     *
     */
    std::bitset<128> noteStates;

    /**
     * @brief Stores the velocity of the note ons for currently
     * depressed notes.
     *
     */
    MidiNoteArray<float> lastNoteVelocities;

    /**
     * @brief Velocity override value (sw_vel in SFZ)
     */
    float velocityOverride;

    /**
     * @brief Last note played
     */
    int lastNotePlayed { -1 };

    /**
     * @brief Current known values for the CCs.
     *
     */
    std::array<EventVector, config::numCCs> ccEvents;

    /**
     * @brief Null event
     *
     */
    const EventVector nullEvent { { 0, 0.0f } };

    /**
     * @brief Pitch bend status
     */
    EventVector pitchEvents;

    /**
     * @brief Aftertouch status
     */
    EventVector channelAftertouchEvents;

    /**
     * @brief Polyphonic aftertouch status.
     */
    std::array<EventVector, 128> polyAftertouchEvents;

    /**
     * @brief The slots whose event vectors received events since the last
     * flush, each once, and whether a slot is among them.
     */
    std::array<uint16_t, numEventSlots> slotsToFlush {};
    std::array<bool, numEventSlots> toFlush {};
    int numSlotsToFlush { 0 };

    /**
     * @brief Current midi program
     */
    int currentProgram { 0 };

    float sampleRate { config::defaultSampleRate };
    int samplesPerBlock { config::defaultSamplesPerBlock };
    float alternate { 0.0f };
    unsigned internalClock { 0 };
    fast_real_distribution<float> unipolarDist { 0.0f, 1.0f };
    fast_real_distribution<float> bipolarDist { -1.0f, 1.0f };
};
}

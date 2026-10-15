// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 30 of aud_audio_pm): a
// layer keeps the modulation targets of its region, which its voices look up
// once instead of at every start.

#pragma once
#include "Region.h"
#include "Config.h"
#include "modulations/ModMatrix.h"
#include "utility/NumericId.h"
#include "utility/LeakDetector.h"
#include <absl/strings/string_view.h>
#include <utility>
#include <string>
#include <vector>
#include <bitset>
#include <memory>

namespace sfz {
struct Region;
class MidiState;

struct Layer {
public:
    /**
     * @brief Initialize a layer based on a new default region.
     */
    Layer(int regionNumber, absl::string_view defaultPath, const MidiState& midiState);

    /**
     * @brief Initialize a layer based on a copy of the contents of a region.
     */
    Layer(const Region& region, const MidiState& midiState);

    ~Layer();

    /**
     * @brief Get the region that this layer operates on.
     */
    const Region& getRegion() const noexcept { return region_; }
    /**
     * @brief Get the region that this layer operates on.
     */
    Region& getRegion() noexcept { return region_; }

    /**
     * @brief Reset the activations to their initial states.
     */
    void initializeActivations();

    /**
     * @brief Given the current midi state, is the region switched on?
     *
     * @return true
     * @return false
     */
    bool isSwitchedOn() const noexcept;
    /**
     * @brief Register a new note on event. The region may be switched on or off using keys so
     * this function updates the keyswitches state.
     *
     * @param noteNumber
     * @param velocity
     * @param randValue a random value between 0 and 1 used to randomize a bit the region activations
     *                  and vary the samples
     * @return true if the region should trigger on this event.
     * @return false
     */
    bool registerNoteOn(int noteNumber, float velocity, float randValue) noexcept;
    /**
     * @brief Register a new note off event. The region may be switched on or off using keys so
     * this function updates the keyswitches state.
     *
     * @param noteNumber
     * @param velocity
     * @param randValue a random value between 0 and 1 used to randomize a bit the region activations
     *                  and vary the samples
     * @return true if the region should trigger on this event.
     * @return false
     */
    bool registerNoteOff(int noteNumber, float velocity, float randValue) noexcept;
    /**
     * @brief Update the internal state of the layer with respect to CC events (sustain, CC
     *  switch, etc).
     *
     * @param ccNumber
     * @param ccValue
     * @return false
     */
    void updateCCState(int ccNumber, float ccValue) noexcept;
    /**
     * @brief Register a new CC event,. This method updates the internal CC state with respect
     * to CC events (sustain, CC switch, etc) and checks if the region should trigger on this
     * event.
     *
     * @param ccNumber
     * @param ccValue
     * @param randValue
     * @param extendedArg is used for special extendedCCs (eg. polyaftertouch to represent note num, etc)
     * @return true if the region should trigger on this event
     * @return false otherwise
     */
    bool registerCC(int ccNumber, float ccValue, float randValue, int extendedArg=-1) noexcept;
    /**
     * @brief Register a new pitch wheel event.
     *
     * @param pitch
     */
    void registerPitchWheel(float pitch) noexcept;
    /**
     * @brief Register a new aftertouch event.
     *
     * @param aftertouch
     */
    void registerAftertouch(float aftertouch) noexcept;
    /**
     * @brief Register tempo
     *
     * @param secondsPerQuarter
     */
    void registerTempo(float secondsPerQuarter) noexcept;
    /**
     * @brief Register program change
     *
     * @param secondsPerQuarter
     */
    void registerProgramChange(int program) noexcept;

    // Started notes
    bool sustainPressed_ { false };
    bool sostenutoPressed_ { false };
    std::vector<std::pair<int, float>> delayedSustainReleases_;
    std::vector<std::pair<int, float>> delayedSostenutoReleases_;
    void delaySustainRelease(int noteNumber, float velocity) noexcept;
    void delaySostenutoRelease(int noteNumber, float velocity) noexcept;
    void storeSostenutoNotes() noexcept;
    void removeFromSostenutoReleases(int noteNumber) noexcept;
    bool isNoteSustained(int noteNumber) const noexcept;
    bool isNoteSostenutoed(int noteNumber) const noexcept;

    const MidiState& midiState_;
    bool keySwitched_ {};
    bool previousKeySwitched_ {};
    bool sequenceSwitched_ {};
    bool pitchSwitched_ {};
    bool programSwitched_ {};
    bool bpmSwitched_ {};
    bool aftertouchSwitched_ {};
    std::bitset<config::numCCs> ccSwitched_;

    int sequenceCounter_ { 0 };

    // The targets of the region in the modulation matrix of the synth, found
    // at the first start of a voice on the layer (aud_dsp_sampler, ticket
    // 30): the matrix is built after the layers and cleared with them.
    struct ModulationTargets {
        bool found { false };
        ModMatrix::TargetId masterAmplitude;
        ModMatrix::TargetId amplitude;
        ModMatrix::TargetId volume;
        ModMatrix::TargetId pan;
        ModMatrix::TargetId position;
        ModMatrix::TargetId width;
        ModMatrix::TargetId pitch;
        ModMatrix::TargetId oscillatorDetune;
        ModMatrix::TargetId oscillatorModDepth;
    };
    ModulationTargets modulationTargets_;

    Region region_;

    LEAK_DETECTOR(Layer);
};

} // namespace sfz

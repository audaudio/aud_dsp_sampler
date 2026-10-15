// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 29 of aud_audio_pm): the
// manager counts the voices that sound, are released and render from its
// list of active voices (countVoices()), and steals sounding voices for a
// voice limit of the host (stealVoices()): released ones before held ones,
// the oldest first, each with its sister voices. A stolen voice is no
// attack voice for a release trigger.
// Ticket 30: the manager keeps a bit per voice that is not idle, so that the
// synth's walks over the voices per block and per event visit only those
// (forEachBusyVoice()), in the order of the list, and a free voice is found
// without a walk.

#pragma once

#include "absl/container/flat_hash_map.h"
#include "Config.h"
#include "PolyphonyGroup.h"
#include "Region.h"
#include "Resources.h"
#include "Voice.h"
#include "VoiceCount.h"
#include "VoiceStealing.h"
#include <cstdint>
#include <vector>

namespace sfz {

struct VoiceManager final : public Voice::StateListener
{
    /**
     * @brief The voice callback which is called during a change of state.
     */
    void onVoiceStateChanging(NumericId<Voice> id, Voice::State state) final;

    /**
     * @brief Find the voice which is associated with the given identifier.
     *
     * @param id
     * @return const Voice*
     */
    const Voice* getVoiceById(NumericId<Voice> id) const noexcept;

    /**
     * @brief Find the voice which is associated with the given identifier.
     *
     * @param id
     * @return Voice*
     */
    Voice* getVoiceById(NumericId<Voice> id) noexcept;

    /**
     * @brief Reset all voices and clear the polyphony groups
     */
    void reset();

    /**
     * @brief Check if a compatible attack voice is playing for the release region.
     *
     * @param releaseRegion
     * @return true
     * @return false
     */
    bool playingAttackVoice(const Region* releaseRegion) noexcept;

    /**
     * @brief Ensures that the polyphony groups are at least this size.
     * Call this each time a new `group=N` is given in an sfz file.
     *
     * @param groupIdx
     */
    void ensureNumPolyphonyGroups(int groupIdx) noexcept;

    /**
     * @brief Set the polyphony for a given group
     * If the number of polyphony groups is too small, it will
     * be increased.
     *
     * @param groupIdx
     * @param polyphony
     */
    void setGroupPolyphony(int groupIdx, unsigned polyphony) noexcept;

    /**
     * @brief Get a view into a given polyphony group
     *
     * @param idx
     * @return const PolyphonyGroup*
     */
    const PolyphonyGroup* getPolyphonyGroupView(int idx) noexcept;

    /**
     * @brief Get the actives voices as a view
     *
     * @return std::vector<const Voice*>
     */
    std::vector<const Voice*> getActiveVoices() const noexcept;

    /**
     * @brief Clear all voices and polyphony groups.
     * Also resets the stealing algorithm to default.
     */
    void clear();

    /**
     * @brief Set the stealing algorithm
     *
     * @param algorithm
     */
    void setStealingAlgorithm(StealingAlgorithm algorithm);

    /**
     * @brief Off voices as necessary depending on the trigger event and started region
     *
     * @param region
     * @param delay
     * @param triggerEvent
     */
    void checkPolyphony(const Region* region, int delay, const TriggerEvent& triggerEvent) noexcept;

    /**
     * @brief Get the number of active voices
     *
     * @return size_t
     */
    size_t getNumActiveVoices() const { return activeVoices_.size(); }

    /**
     * @brief Count the voices that sound, those of them that are released
     * and those that render, from the active voices only (aud_dsp_sampler,
     * ticket 29). Allocates nothing.
     *
     * @return VoiceCount
     */
    VoiceCount countVoices() const noexcept;

    /**
     * @brief Steal at least `count` sounding voices, or all of them, for a
     * voice limit of the host (aud_dsp_sampler, ticket 29): released voices
     * before held ones, the oldest first, each with its sister voices, which
     * may take more. Each fades out over `fadeFrames` (Voice::steal()).
     * Allocates nothing.
     *
     * @param count the sounding voices to steal at least
     * @param fadeFrames the frames of the fade, at least 1
     * @return VoiceCount the sounding voices it stole and the released ones
     *         among them
     */
    VoiceCount stealVoices(int count, int fadeFrames) noexcept;

    /**
     * @brief Get the number of polyphony groups
     *
     * @return size_t
     */
    size_t getNumPolyphonyGroups() const noexcept { return polyphonyGroups_.size(); }

    /**
     * @brief Find a voice that is not currently playing
     *
     * @return Voice*
     */
    Voice* findFreeVoice() noexcept;

    /**
     * @brief Call `visit` with each voice that is not idle, in the order of
     * the list (aud_dsp_sampler, ticket 30). It visits what a walk over the
     * whole list that skips the free voices visits: a voice that a visit
     * starts behind the current one is visited too, one that it frees is
     * not. A visit that returns true ends the walk.
     *
     * @param visit a functor with signature bool(Voice&)
     * @return true if a visit ended the walk
     */
    template <class F>
    bool forEachBusyVoice(F&& visit)
    {
        for (size_t word = 0, words = busy_.size(); word < words; ++word) {
            uint64_t bits = busy_[word];
            while (bits != 0) {
                const unsigned bit = lowestBit(bits);
                if (visit(list_[word * 64 + bit]))
                    return true;
                // The visit may have started or freed voices.
                bits = busy_[word] & (~uint64_t { 0 } << bit << 1);
            }
        }
        return false;
    }
    /**
     * @brief Require a number of voices from this manager.
     * In practice, the manager will handle slightly more, in order to
     * allow voices to die off upon reaching higher polyphony count.
     *
     * @param numVoices
     * @param resources
     */
    void requireNumVoices(int numVoices, Resources& resources);

    /**
     * @brief Is this timestamp within the lo/hitimer range for this region based on current group activity
     *
     * @param region
     * @param timestampSamples in samples
     * @param sampleRate
     * @return bool
     */
    bool withinValidTimerRange(const Region* region, unsigned timestampSamples, float sampleRate) const noexcept;

private:
    int numRequiredVoices_ { config::numVoices };
    std::vector<Voice> list_;
    std::vector<Voice*> activeVoices_;
    std::vector<Voice*> temp_;
    // A bit per voice of the list that is not idle: playing, or to be
    // cleaned up (aud_dsp_sampler, ticket 30). A voice of a disabled region
    // goes from idle to clean up without playing, so it is not among the
    // active voices, but it renders once.
    std::vector<uint64_t> busy_;

    void setBusy(const Voice* voice, bool busy) noexcept;

    static unsigned lowestBit(uint64_t bits) noexcept
    {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<unsigned>(__builtin_ctzll(bits));
#else
        unsigned bit = 0;
        while ((bits & 1) == 0) {
            bits >>= 1;
            ++bit;
        }
        return bit;
#endif
    }
    // These are the `group=` groups where you can off voices
    absl::flat_hash_map<int, PolyphonyGroup> polyphonyGroups_;
    std::unique_ptr<VoiceStealer> stealer_ { absl::make_unique<OldestStealer>() };

    /**
     * @brief Check the region polyphony, releasing voices if necessary
     *
     * @param region
     * @param delay
     */
    void checkRegionPolyphony(const Region* region, int delay) noexcept;

    /**
     * @brief Check the note polyphony, releasing voices if necessary
     *
     * @param region
     * @param delay
     * @param triggerEvent
     */
    void checkNotePolyphony(const Region* region, int delay, const TriggerEvent& triggerEvent) noexcept;

    /**
     * @brief Check the group polyphony, releasing voices if necessary
     *
     * @param region
     * @param delay
     */
    void checkGroupPolyphony(const Region* region, int delay) noexcept;

    /**
     * @brief Check the region set polyphony at all levels, releasing voices if necessary
     *
     * @param region
     * @param delay
     */
    void checkSetPolyphony(const Region* region, int delay) noexcept;

    /**
     * @brief Check the engine polyphony, fast releasing voices if necessary
     *
     * @param delay
     */
    void checkEnginePolyphony(int delay) noexcept;

public:
    // Vector shortcuts
    typename decltype(list_)::iterator begin() { return list_.begin(); }
    typename decltype(list_)::const_iterator cbegin() const { return list_.cbegin(); }
    typename decltype(list_)::iterator end() { return list_.end(); }
    typename decltype(list_)::const_iterator cend() const { return list_.cend(); }
    typename decltype(list_)::reference operator[] (size_t n) { return list_[n]; }
    typename decltype(list_)::const_reference operator[] (size_t n) const { return list_[n]; }
};

} // namespace sfz

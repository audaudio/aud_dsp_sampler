// Modified by Audanika for aud_dsp_sampler (ticket 25 of aud_audio_pm): a
// set lets go of a region the load drops.

#include "RegionSet.h"

sfz::RegionSet::RegionSet(RegionSet* parentSet, OpcodeScope level)
    : parent(parentSet), level(level)
{
    voices.reserve(config::maxVoices);
    if (parentSet != nullptr)
        parentSet->addSubset(this);
}

void sfz::RegionSet::setPolyphonyLimit(unsigned limit) noexcept
{
    polyphonyLimit = limit;
}

void sfz::RegionSet::addRegion(Region* region) noexcept
{
    if (absl::c_find(regions, region) == regions.end())
        regions.push_back(region);
}

void sfz::RegionSet::removeRegion(const Region* region) noexcept
{
    const auto it = absl::c_find(regions, region);
    if (it != regions.end())
        regions.erase(it);
}

void sfz::RegionSet::addSubset(RegionSet* group) noexcept
{
    if (absl::c_find(subsets, group) == subsets.end())
        subsets.push_back(group);
}

void sfz::RegionSet::registerVoice(Voice* voice) noexcept
{
    if (absl::c_find(voices, voice) == voices.end())
        voices.push_back(voice);
}

void sfz::RegionSet::removeVoice(const Voice* voice) noexcept
{
    swapAndPopFirst(voices, [voice](const Voice* v) { return v == voice; });
}

void sfz::RegionSet::registerVoiceInHierarchy(const Region* region, Voice* voice) noexcept
{
    auto* parent = region->parent;
    while (parent != nullptr) {
        parent->registerVoice(voice);
        parent = parent->getParent();
    }
}

void sfz::RegionSet::removeVoiceFromHierarchy(const Region* region, const Voice* voice) noexcept
{
    auto* parent = region->parent;
    while (parent != nullptr) {
        parent->removeVoice(voice);
        parent = parent->getParent();
    }
}

unsigned sfz::RegionSet::numPlayingVoices() const noexcept
{
    return absl::c_count_if(voices, [](const Voice* v) {
        return !v->offedOrFree();
    });
}

void sfz::RegionSet::removeAllVoices() noexcept
{
    voices.clear();
}

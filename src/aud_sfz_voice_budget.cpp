// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_sfz_voice_budget.hpp"

#include <algorithm>
#include <type_traits>

namespace aud_sfz {
namespace {

// Counters, not guards: no other memory hangs on them.
constexpr std::memory_order kRelaxed = std::memory_order_relaxed;

// `excess` × part / whole rounded down, but 1 for a part whose share is at
// least the average of `nodes` nodes (part × nodes >= whole).
int64_t shareOf(int64_t excess, int64_t part, int64_t whole, int64_t nodes) {
  if (excess <= 0 || part <= 0 || whole <= 0) return 0;
  const int64_t share = excess * part / whole;
  if (share == 0 && part * nodes >= whole) return 1;
  return share;
}

}  // namespace

// ############################################################################
// Counts

int32_t limitExcess(const VoiceTally& own, int32_t maxVoices, int32_t reserve) {
  if (maxVoices >= kNoVoiceLimit) return 0;
  return std::max(0, own.sounding - std::max(1, maxVoices - reserve));
}

int32_t budgetShare(const VoiceTally& own, const VoiceProcess& process) {
  if (process.budget <= 0 || own.sounding <= 0) return 0;
  // Another audio thread may publish between the loads: no count below 0
  // and no more released voices than sounding ones.
  const int64_t othersSounding = std::max(0, process.others.sounding);
  const int64_t othersReleased =
      std::min<int64_t>(othersSounding, std::max(0, process.others.released));
  const int64_t sounding = othersSounding + own.sounding;
  const int64_t released = othersReleased + own.released;
  const int64_t excess = sounding - std::max(1, process.budget - process.reserve);
  // This node has voices, so it counts among the nodes.
  const int64_t nodes = std::max(0, process.nodes) + 1;
  if (excess <= released) {
    return static_cast<int32_t>(shareOf(excess, own.released, released, nodes));
  }
  const int64_t held = shareOf(excess - released, own.sounding - own.released,
                               sounding - released, nodes);
  return static_cast<int32_t>(own.released + held);
}

// ############################################################################
// The budget of the process

// Atomics only: nothing runs when the process ends.
static_assert(std::is_trivially_destructible<VoiceBudget>::value,
              "the budget of the process outlives every node");
// The counts take no lock on the audio thread, also on 32-bit targets.
static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2,
              "the voice counters must be lock-free");

VoiceBudget& VoiceBudget::get() {
  // Constant-initialized: no guard and no allocation on the first call.
  static VoiceBudget budget;
  return budget;
}

void VoiceBudget::setBudget(int32_t voices) { budget_.store(std::max(0, voices), kRelaxed); }

int32_t VoiceBudget::budget() const { return budget_.load(kRelaxed); }

void VoiceBudget::setReserve(int32_t voices) {
  reserve_.store(std::min(kMaxVoiceReserve, std::max(0, voices)), kRelaxed);
}

int32_t VoiceBudget::reserve() const { return reserve_.load(kRelaxed); }

AudSfzVoiceStats VoiceBudget::stats() const {
  AudSfzVoiceStats stats{};
  stats.struct_size = sizeof(AudSfzVoiceStats);
  stats.budget = budget();
  stats.reserve = reserve();
  stats.sounding = sounding_.load(kRelaxed);
  stats.released = released_.load(kRelaxed);
  stats.stolen = stolen_.load(kRelaxed);
  stats.voice_frames = voiceFrames_.load(kRelaxed);
  return stats;
}

VoiceProcess VoiceBudget::processFor(const VoicePublication& published) const {
  VoiceProcess process;
  process.others.sounding = sounding_.load(kRelaxed) - published.voices.sounding;
  process.others.released = released_.load(kRelaxed) - published.voices.released;
  process.nodes = nodes_.load(kRelaxed) - (published.counted ? 1 : 0);
  process.budget = budget();
  process.reserve = reserve();
  return process;
}

void VoiceBudget::publish(VoicePublication& published, const VoiceTally& voices, uint32_t stolen,
                          uint64_t voiceFrames) {
  replace(published, voices);
  if (stolen > 0) stolen_.fetch_add(stolen, kRelaxed);
  if (voiceFrames > 0) voiceFrames_.fetch_add(voiceFrames, kRelaxed);
}

void VoiceBudget::withdraw(VoicePublication& published) { replace(published, VoiceTally{}); }

void VoiceBudget::replace(VoicePublication& published, const VoiceTally& voices) {
  const int32_t sounding = voices.sounding - published.voices.sounding;
  if (sounding != 0) sounding_.fetch_add(sounding, kRelaxed);
  const int32_t released = voices.released - published.voices.released;
  if (released != 0) released_.fetch_add(released, kRelaxed);
  const bool counted = voices.sounding > 0;
  if (counted != published.counted) nodes_.fetch_add(counted ? 1 : -1, kRelaxed);
  published.voices = voices;
  published.counted = counted;
}

}  // namespace aud_sfz

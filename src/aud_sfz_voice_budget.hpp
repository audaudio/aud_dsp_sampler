// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The voice limit of a sampler node and the voice budget of the process
// (ticket 29): how many voices a node steals at the end of a block, and the
// atomics through which the nodes of all graphs share their voices. Written
// in C++14 like the node.

#ifndef AUD_SFZ_VOICE_BUDGET_HPP
#define AUD_SFZ_VOICE_BUDGET_HPP

#include <atomic>
#include <cstdint>

#include "aud_dsp_sampler.h"

namespace aud_sfz {

// ############################################################################
// Constants

// The max_voices that sets no limit beyond sfizz's polyphony.
constexpr int32_t kNoVoiceLimit = 256;
// The free voices kept below each limit, by default and at most.
constexpr int32_t kDefaultVoiceReserve = 4;
constexpr int32_t kMaxVoiceReserve = 16;
// The fade of a stolen voice.
constexpr double kStealSeconds = 0.010;

// ############################################################################
// Counts

// The voices of a node, or of several.
struct VoiceTally {
  int32_t sounding = 0;  // play, neither turned off nor stolen
  int32_t released = 0;  // of them released: key and pedal up
};

// What a node published to the budget last; it withdraws it when it goes.
struct VoicePublication {
  VoiceTally voices;
  bool counted = false;  // among the nodes with sounding voices
};

// The process as one node sees it: what the other nodes published, and the
// budget with its reserve.
struct VoiceProcess {
  VoiceTally others;
  int32_t nodes = 0;   // other nodes with sounding voices
  int32_t budget = 0;  // 0: none
  int32_t reserve = kDefaultVoiceReserve;
};

// The voices of `own` beyond max(1, maxVoices - reserve); none from
// kNoVoiceLimit on.
int32_t limitExcess(const VoiceTally& own, int32_t maxVoices, int32_t reserve);

// The voices a node takes of the excess of the process over
// max(1, budget - reserve): released voices first, shared out by the
// released voices of each node; when the excess is larger, all its released
// voices and a share of the rest by the held voices of each node. A share
// rounds down, but a node whose share is at least the average of the nodes
// with voices takes at least one, so that the excess shrinks with every
// round of blocks and a node with few voices is not the one robbed.
int32_t budgetShare(const VoiceTally& own, const VoiceProcess& process);

// ############################################################################
// The budget of the process

// The voice budget, the reserve and the voices of every sampler node of the
// process, as atomics: Dart sets the budget and reads the numbers on any
// thread while the audio threads publish.
class VoiceBudget {
 public:
  // A budget of its own without a limit, e.g. for a test; the nodes use
  // get(). Constant: a static budget needs no code to start.
  constexpr VoiceBudget() = default;

  // The one budget of the process. It is built before main and has nothing
  // to destroy: the audio thread that asks first allocates nothing, and a
  // node of an engine that outlives the statics at exit may still withdraw.
  static VoiceBudget& get();

  // [any thread] The budget in voices; 0 and below: none.
  void setBudget(int32_t voices);
  int32_t budget() const;

  // [any thread] The free voices below each limit, clamped to 0 to 16.
  void setReserve(int32_t voices);
  int32_t reserve() const;

  // [any thread] The budget, the reserve, the voices the nodes published
  // and the counters.
  AudSfzVoiceStats stats() const;

  // [realtime] The process as the node that published `published` sees it.
  VoiceProcess processFor(const VoicePublication& published) const;

  // [realtime] Publishes the voices of a node's live block in place of what
  // it published before, and counts what it stole and rendered.
  void publish(VoicePublication& published, const VoiceTally& voices, uint32_t stolen,
               uint64_t voiceFrames);

  // [any thread] Withdraws what a node published: it goes, renders
  // offline or was reset.
  void withdraw(VoicePublication& published);

 private:
  // Replaces what a node published with `voices`, as differences.
  void replace(VoicePublication& published, const VoiceTally& voices);

  std::atomic<int32_t> budget_{0};
  std::atomic<int32_t> reserve_{kDefaultVoiceReserve};
  std::atomic<int32_t> sounding_{0};
  std::atomic<int32_t> released_{0};
  std::atomic<int32_t> nodes_{0};
  std::atomic<uint64_t> stolen_{0};
  std::atomic<uint64_t> voiceFrames_{0};
};

}  // namespace aud_sfz

#endif  // AUD_SFZ_VOICE_BUDGET_HPP

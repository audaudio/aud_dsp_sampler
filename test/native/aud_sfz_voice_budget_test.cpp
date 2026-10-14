// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the voice budget of the process (src/aud_sfz_voice_budget.cpp,
// ticket 29): the voices a node steals for max_voices and for its share of
// the excess over the budget, the atomics through which the nodes publish
// their voices, the C API, and the budget across the nodes of a graph and
// across graphs, also while another thread sets it.

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "aud_sampler_fixture.hpp"
#include "aud_sfz_voice_budget.hpp"

using namespace aud_sampler_test;
using aud_sfz::VoiceBudget;
using aud_sfz::VoiceProcess;
using aud_sfz::VoicePublication;
using aud_sfz::VoiceTally;

namespace {

VoiceTally tally(int32_t sounding, int32_t released) {
  VoiceTally voices;
  voices.sounding = sounding;
  voices.released = released;
  return voices;
}

VoiceProcess process(int32_t budget, int32_t reserve, VoiceTally others, int32_t nodes) {
  VoiceProcess result;
  result.budget = budget;
  result.reserve = reserve;
  result.others = others;
  result.nodes = nodes;
  return result;
}

// An instrument of sines without files whose released voices sound for two
// seconds.
const char* const kSines = "<region> sample=*sine ampeg_release=2\n";

// A sampler with the sines on a graph that runs.
int32_t sinesNode(Fixture& f) {
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, "/aud_dsp_sampler/sines.sfz", kSines));
  return node;
}

// Starts `count` notes from `first` on, every second key, in one block.
void startNotes(Fixture& f, int32_t node, uint32_t count, uint32_t first = 60) {
  for (uint32_t i = 0; i < count; ++i) f.noteOn(node, first + 2 * i);
}

}  // namespace

// ############################################################################
// limitExcess

AUD_TEST(voice_budget_limit_excess_keeps_the_reserve_below_max_voices) {
  // 256 sets no limit; below it the reserve comes off, down to one voice.
  AUD_CHECK(aud_sfz::limitExcess(tally(200, 0), 256, 4) == 0);
  AUD_CHECK(aud_sfz::limitExcess(tally(8, 3), 10, 4) == 2);
  AUD_CHECK(aud_sfz::limitExcess(tally(6, 0), 10, 4) == 0);
  AUD_CHECK(aud_sfz::limitExcess(tally(5, 0), 3, 4) == 4);
  AUD_CHECK(aud_sfz::limitExcess(tally(5, 0), 1, 0) == 4);
  AUD_CHECK(aud_sfz::limitExcess(tally(0, 0), 1, 0) == 0);
}

// ############################################################################
// budgetShare

AUD_TEST(voice_budget_share_is_none_without_a_budget_or_an_excess) {
  AUD_CHECK(aud_sfz::budgetShare(tally(50, 10), process(0, 4, tally(50, 0), 1)) == 0);
  AUD_CHECK(aud_sfz::budgetShare(tally(0, 0), process(1, 0, tally(50, 0), 1)) == 0);
  AUD_CHECK(aud_sfz::budgetShare(tally(4, 0), process(12, 4, tally(4, 0), 1)) == 0);
}

AUD_TEST(voice_budget_share_takes_released_voices_first) {
  // 16 sound, 12 are allowed: the excess of 4 comes from the 8 released
  // voices, half of them this node's.
  AUD_CHECK(aud_sfz::budgetShare(tally(8, 4), process(12, 0, tally(8, 4), 1)) == 2);
  // The node without released voices keeps its voices.
  AUD_CHECK(aud_sfz::budgetShare(tally(8, 0), process(12, 0, tally(8, 8), 1)) == 0);
  // The reserve lowers the budget: 16 sound, 8 are allowed.
  AUD_CHECK(aud_sfz::budgetShare(tally(8, 4), process(12, 4, tally(8, 4), 1)) == 4);
}

AUD_TEST(voice_budget_share_takes_held_voices_by_their_share) {
  // 20 sound, 10 are allowed, 4 are released: this node gives its 2
  // released voices and 6/16 of the 6 held ones that are left over, 2.25.
  AUD_CHECK(aud_sfz::budgetShare(tally(8, 2), process(10, 0, tally(12, 2), 1)) == 4);
  // Everything above one voice when the reserve eats the budget.
  AUD_CHECK(aud_sfz::budgetShare(tally(5, 0), process(3, 4, tally(0, 0), 0)) == 4);
}

AUD_TEST(voice_budget_share_takes_one_when_the_floor_is_zero_and_the_share_is_large) {
  // One voice too many of 100: the node with 99 of them takes it, the node
  // with one does not, and of two equal nodes each would.
  AUD_CHECK(aud_sfz::budgetShare(tally(99, 0), process(99, 0, tally(1, 0), 1)) == 1);
  AUD_CHECK(aud_sfz::budgetShare(tally(1, 0), process(99, 0, tally(99, 0), 1)) == 0);
  AUD_CHECK(aud_sfz::budgetShare(tally(50, 0), process(99, 0, tally(50, 0), 1)) == 1);
  // The same for released voices.
  AUD_CHECK(aud_sfz::budgetShare(tally(60, 30), process(99, 0, tally(40, 30), 1)) == 1);
  AUD_CHECK(aud_sfz::budgetShare(tally(60, 1), process(99, 0, tally(40, 59), 1)) == 0);
}

AUD_TEST(voice_budget_share_clamps_what_other_threads_published_meanwhile) {
  // A count below 0 counts as 0, more released than sounding voices as
  // the sounding ones.
  AUD_CHECK(aud_sfz::budgetShare(tally(6, 0), process(4, 0, tally(-3, -1), -1)) == 2);
  AUD_CHECK(aud_sfz::budgetShare(tally(6, 0), process(10, 0, tally(6, 9), 1)) == 0);
}

// ############################################################################
// VoiceBudget

AUD_TEST(voice_budget_clamps_the_budget_and_the_reserve) {
  VoiceBudget budget;
  AUD_CHECK(budget.budget() == 0 && budget.reserve() == aud_sfz::kDefaultVoiceReserve);
  budget.setBudget(48);
  AUD_CHECK(budget.budget() == 48);
  budget.setBudget(-1);
  AUD_CHECK(budget.budget() == 0);
  budget.setReserve(-2);
  AUD_CHECK(budget.reserve() == 0);
  budget.setReserve(100);
  AUD_CHECK(budget.reserve() == aud_sfz::kMaxVoiceReserve);
  budget.setReserve(3);
  const AudSfzVoiceStats stats = budget.stats();
  AUD_CHECK(stats.struct_size == sizeof(AudSfzVoiceStats));
  AUD_CHECK(stats.budget == 0 && stats.reserve == 3 && stats.reserved == 0);
}

AUD_TEST(voice_budget_publishes_differences_and_withdraws_them) {
  VoiceBudget budget;
  budget.setBudget(20);
  VoicePublication a;
  VoicePublication b;
  budget.publish(a, tally(6, 2), 3, 1000);
  budget.publish(b, tally(4, 1), 0, 0);
  AudSfzVoiceStats stats = budget.stats();
  AUD_CHECK(stats.sounding == 10 && stats.released == 3);
  AUD_CHECK(stats.stolen == 3 && stats.voice_frames == 1000);
  // A node sees what the others published, without its own.
  VoiceProcess seen = budget.processFor(a);
  AUD_CHECK(seen.others.sounding == 4 && seen.others.released == 1 && seen.nodes == 1);
  AUD_CHECK(seen.budget == 20 && seen.reserve == aud_sfz::kDefaultVoiceReserve);
  // A node without sounding voices is no node with voices.
  budget.publish(a, tally(0, 0), 1, 256);
  seen = budget.processFor(b);
  AUD_CHECK(seen.others.sounding == 0 && seen.nodes == 0);
  stats = budget.stats();
  AUD_CHECK(stats.sounding == 4 && stats.released == 1);
  AUD_CHECK(stats.stolen == 4 && stats.voice_frames == 1256);
  // Withdrawn twice, the second time changes nothing; the counters stay.
  budget.withdraw(b);
  budget.withdraw(b);
  budget.withdraw(a);
  stats = budget.stats();
  AUD_CHECK(stats.sounding == 0 && stats.released == 0);
  AUD_CHECK(stats.stolen == 4 && stats.voice_frames == 1256);
  AUD_CHECK(budget.processFor(VoicePublication()).nodes == 0);
}

// ############################################################################
// The C API

AUD_TEST(voice_budget_c_api_sets_and_reads_the_budget_of_the_process) {
  VoiceBudgetScope scope(0, 4);
  aud_dsp_sampler_set_voice_budget(32);
  AUD_CHECK(aud_dsp_sampler_voice_budget() == 32);
  AUD_CHECK(&VoiceBudget::get() == &VoiceBudget::get());
  AUD_CHECK(VoiceBudget::get().budget() == 32);
  aud_dsp_sampler_set_voice_budget(-7);
  AUD_CHECK(aud_dsp_sampler_voice_budget() == 0);
  aud_dsp_sampler_set_voice_reserve(17);
  AUD_CHECK(aud_dsp_sampler_voice_reserve() == 16);
  aud_dsp_sampler_set_voice_reserve(-1);
  AUD_CHECK(aud_dsp_sampler_voice_reserve() == 0);
  aud_dsp_sampler_set_voice_reserve(5);
  AUD_CHECK(voiceStats().reserve == 5);
  // NULL and a struct that is too small are refused; a larger one keeps
  // its size.
  AUD_CHECK(aud_dsp_sampler_voice_stats(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AudSfzVoiceStats small{};
  small.struct_size = sizeof(AudSfzVoiceStats) - 1;
  AUD_CHECK(aud_dsp_sampler_voice_stats(&small) == AUD_ERROR_INVALID_ARGUMENT);
  struct Larger {
    AudSfzVoiceStats stats;
    uint64_t more;
  } larger{};
  larger.stats.struct_size = sizeof(Larger);
  AUD_CHECK(aud_dsp_sampler_voice_stats(&larger.stats) == AUD_OK);
  AUD_CHECK(larger.stats.struct_size == sizeof(Larger) && larger.stats.reserve == 5);
}

// ############################################################################
// The nodes of a graph

AUD_TEST(voice_budget_holds_across_the_nodes_of_a_graph_released_voices_first) {
  VoiceBudgetScope scope(0, 0);
  Fixture f;
  const int32_t a = sinesNode(f);
  const int32_t b = sinesNode(f);
  f.renderLive(256);
  const uint64_t stolen = voiceStats().stolen;
  startNotes(f, a, 6);
  startNotes(f, b, 4);
  f.renderLive(256);
  f.noteOff(a, 68);
  f.noteOff(a, 70);
  f.renderLive(256);
  AUD_CHECK(voiceStats().sounding == 10 && voiceStats().released == 2);
  // Two voices too many: they are the released ones, both of a.
  aud_dsp_sampler_set_voice_budget(8);
  f.renderLive(256);
  AUD_CHECK(voiceStats().sounding == 8 && voiceStats().released == 0);
  AUD_CHECK(voiceStats().stolen == stolen + 2);
  f.renderLive(1024);
  AUD_CHECK(voiceNotes(f, a) == (std::vector<int64_t>{60, 62, 64, 66}));
  AUD_CHECK(voiceNotes(f, b) == (std::vector<int64_t>{60, 62, 64, 66}));
  // Two more: the held voices of both nodes, by their shares.
  aud_dsp_sampler_set_voice_budget(6);
  f.renderLive(512);
  AUD_CHECK(voiceStats().sounding == 6);
  AUD_CHECK(voiceStats().stolen == stolen + 4);
  f.renderLive(1024);
  AUD_CHECK(voiceNotes(f, a) == (std::vector<int64_t>{62, 64, 66}));
  AUD_CHECK(voiceNotes(f, b) == (std::vector<int64_t>{62, 64, 66}));
  // Budget 0 lifts it: new notes sound again.
  aud_dsp_sampler_set_voice_budget(0);
  startNotes(f, b, 3, 80);
  f.renderLive(256);
  AUD_CHECK(voiceStats().sounding == 9);
  AUD_CHECK(voiceStats().stolen == stolen + 4);
}

AUD_TEST(voice_budget_keeps_the_reserve_free) {
  VoiceBudgetScope scope(10, 4);
  Fixture f;
  const int32_t node = sinesNode(f);
  f.renderLive(256);
  startNotes(f, node, 12);
  f.renderLive(256);
  AUD_CHECK(voiceStats().sounding == 6);
  // Without a reserve the whole budget sounds.
  aud_dsp_sampler_set_voice_reserve(0);
  startNotes(f, node, 6, 90);
  f.renderLive(256);
  AUD_CHECK(voiceStats().sounding == 10);
  // A reserve as large as the budget leaves one voice.
  aud_dsp_sampler_set_voice_reserve(16);
  f.renderLive(256);
  AUD_CHECK(voiceStats().sounding == 1);
}

AUD_TEST(voice_budget_counts_rendered_voices_and_frames_of_live_blocks) {
  VoiceBudgetScope scope(0, 0);
  Fixture f;
  const int32_t node = sinesNode(f);
  f.renderLive(256);
  const AudSfzVoiceStats before = voiceStats();
  startNotes(f, node, 3);
  f.renderLive(512);
  // Three voices in two blocks of 256.
  AUD_CHECK(voiceStats().voice_frames == before.voice_frames + 3 * 512);
  // A stolen voice renders its fade: it counts until it is free.
  f.param(node, AUD_SFZ_PARAM_MAX_VOICES, 2);
  f.renderLive(256);
  AUD_CHECK(voiceStats().voice_frames == before.voice_frames + 3 * 512 + 3 * 256);
  f.renderLive(512);
  f.renderLive(256);
  AUD_CHECK(voiceStats().voice_frames == before.voice_frames + 3 * 1024 + 2 * 512);
  // Offline blocks count nothing.
  f.renderOffline(1024);
  AUD_CHECK(voiceStats().voice_frames == before.voice_frames + 3 * 1024 + 2 * 512);
}

AUD_TEST(voice_budget_forgets_a_node_that_goes_stops_or_renders_offline) {
  VoiceBudgetScope scope(0, 0);
  const int32_t before = voiceStats().sounding;
  {
    Fixture f;
    const int32_t a = sinesNode(f);
    const int32_t b = sinesNode(f);
    f.renderLive(256);
    startNotes(f, a, 3);
    startNotes(f, b, 2);
    f.renderLive(256);
    AUD_CHECK(voiceStats().sounding == before + 5);
    // An offline block withdraws, the next live block publishes again.
    f.renderOffline(256);
    AUD_CHECK(voiceStats().sounding == before);
    f.renderLive(256);
    AUD_CHECK(voiceStats().sounding == before + 5);
    // A stop resets the nodes: their voices are gone.
    AUD_CHECK(aud_graph_stop(f.graph) == AUD_OK);
    AUD_CHECK(voiceStats().sounding == before);
    AUD_CHECK(aud_graph_start(f.graph) == AUD_OK);
    // The first block after the restart sends the note-offs of the notes
    // the graph tracked.
    f.renderLive(256);
    AUD_CHECK(voiceStats().sounding == before);
    startNotes(f, a, 3);
    startNotes(f, b, 2);
    f.renderLive(256);
    AUD_CHECK(voiceStats().sounding == before + 5);
    // A removed node goes after its tail, with its voices: the graph
    // compiles a program without it, renders it and frees the node at the
    // next transaction.
    f.remove(a);
    f.renderLive(48000 * 3);
    for (int round = 0; round < 2; ++round) {
      f.renderLive(256);
      f.commit();
    }
    AUD_CHECK(voiceStats().sounding == before + 2);
    // The graph goes with the node that sounds.
  }
  AUD_CHECK(voiceStats().sounding == before);
  AUD_CHECK(voiceStats().released == 0);
}

// ############################################################################
// Graphs

AUD_TEST(voice_budget_holds_across_graphs) {
  VoiceBudgetScope scope(4, 0);
  Fixture f;
  Fixture g;
  const int32_t a = sinesNode(f);
  const int32_t b = sinesNode(g);
  f.renderLive(256);
  g.renderLive(256);
  startNotes(f, a, 4);
  startNotes(g, b, 4);
  // The graphs render one after the other, as two streams on one core.
  for (int block = 0; block < 4; ++block) {
    f.renderLive(256);
    g.renderLive(256);
  }
  AUD_CHECK(voiceStats().sounding == 4);
  AUD_CHECK(voiceNotes(f, a).size() == 2);
  AUD_CHECK(voiceNotes(g, b).size() == 2);
}

AUD_TEST(voice_budget_holds_while_graphs_render_on_two_threads) {
  // Two streams on two threads, and a control thread that sets the budget
  // and the reserve and reads the numbers meanwhile (ThreadSanitizer).
  VoiceBudgetScope scope(0, 0);
  Fixture f;
  Fixture g;
  const int32_t a = sinesNode(f);
  const int32_t b = sinesNode(g);
  f.renderLive(256);
  g.renderLive(256);
  startNotes(f, a, 12);
  startNotes(g, b, 12);
  std::atomic<bool> done{false};
  std::atomic<int> outOfRange{0};
  std::atomic<int> rounds{0};
  std::thread control([&done, &outOfRange, &rounds] {
    while (!done.load()) {
      const int round = rounds.fetch_add(1);
      aud_dsp_sampler_set_voice_budget(round % 2 == 0 ? 16 : 0);
      aud_dsp_sampler_set_voice_reserve(round % 3);
      AudSfzVoiceStats stats{};
      stats.struct_size = sizeof(AudSfzVoiceStats);
      // Each node publishes its whole count at once: the sum stays between
      // nothing and all 24 voices.
      if (aud_dsp_sampler_voice_stats(&stats) != AUD_OK || stats.sounding < 0 ||
          stats.sounding > 24 || stats.released != 0) {
        outOfRange.fetch_add(1);
      }
      std::this_thread::yield();
    }
  });
  std::thread other([&g] { g.renderLive(256 * 200); });
  f.renderLive(256 * 200);
  other.join();
  done.store(true);
  control.join();
  AUD_CHECK(outOfRange.load() == 0);
  AUD_CHECK(rounds.load() > 0);
  // Settled on one thread: the count is exact again.
  aud_dsp_sampler_set_voice_budget(8);
  aud_dsp_sampler_set_voice_reserve(0);
  for (int block = 0; block < 8; ++block) {
    f.renderLive(256);
    g.renderLive(256);
  }
  AUD_CHECK(voiceStats().sounding == 8);
}

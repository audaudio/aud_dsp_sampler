// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the wavetables of the sfizz fork (src/sfizz/Wavetables.cpp): the
// silent wavetable, which every voice of the process starts with, is built
// once, also when several threads ask for it first at the same time
// (ticket 28).

#include <atomic>
#include <thread>
#include <vector>

#include "Wavetables.h"

#include "aud_test.hpp"

// ############################################################################
// The silent wavetable

AUD_TEST(sfizz_wavetables_build_the_silence_once_for_threads_at_once) {
  // The voices of synths that load at the same time ask for it as the
  // threads do here. Before, two threads that found it unbuilt both built
  // it into the one table; a race that happens once per process, so the
  // test proves it when it runs alone and asks first.
  constexpr int kThreads = 4;
  std::atomic<bool> start{false};
  std::vector<const sfz::WavetableMulti*> silences(kThreads, nullptr);
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start.load()) std::this_thread::yield();
      silences[t] = sfz::WavetableMulti::getSilenceWavetable();
    });
  }
  start.store(true);
  for (std::thread& thread : threads) thread.join();
  const sfz::WavetableMulti* silence = sfz::WavetableMulti::getSilenceWavetable();
  AUD_CHECK(silences == std::vector<const sfz::WavetableMulti*>(kThreads, silence));
  AUD_CHECK(silence->tableSize() == 1);
  bool silent = true;
  for (unsigned m = 0; m < sfz::WavetableMulti::numTables(); ++m) {
    for (float value : silence->getTable(m)) silent = silent && value == 0.0f;
  }
  AUD_CHECK(silent);
}

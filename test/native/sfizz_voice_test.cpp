// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the voices of the sfizz fork (src/sfizz/Voice.cpp) on a synth of
// their own, without the node: a voice whose file the pool does not hold.
//
// The pool of the synth reports its bytes to nobody. The test first lets the
// engine trim what the store holds for it, so that the store then holds only
// what the test adds, and frees that itself at its end.

#include <cstdint>
#include <limits>
#include <vector>

#include "aud_audio_graph.h"

#include "AudioSpan.h"
#include "FilePool.h"
#include "Synth.h"

#include "aud_sampler_fixture.hpp"

using namespace aud_sampler_test;

// ############################################################################
// A voice without its file

AUD_TEST(sfizz_voice_without_its_file_in_the_pool_stays_idle) {
  const uint64_t budget = aud_cache_budget();
  aud_cache_set_budget(1);
  aud_cache_set_budget(budget);
  TempFolder folder;
  writeWav(folder.file("held.wav"), sine(375, 0.5));
  {
    sfz::Synth synth;
    synth.setSamplesPerBlock(256);
    AUD_CHECK(synth.loadSfzString(folder.file("held.sfz"), "<region> sample=held.wav key=60\n"));
    // The pool lets go of the file the region plays: the voice of a note
    // goes back to idle at once instead of rendering a block without its
    // modulation targets.
    synth.getResources().getFilePool().clear();
    synth.noteOn(0, 60, 100);
    AUD_CHECK(synth.getNumActiveVoices() == 0);
    std::vector<float> left(256);
    std::vector<float> right(256);
    float* channels[2] = {left.data(), right.data()};
    synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, 256));
    AUD_CHECK(peak(left, 0, left.size()) == 0.0f);
  }
  sfz::SampleStore::get().trim(std::numeric_limits<uint64_t>::max());
}

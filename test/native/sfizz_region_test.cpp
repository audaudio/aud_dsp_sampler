// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the regions of the sfizz fork (src/sfizz/Region.cpp) on a synth of
// their own, without the node: the opcodes of a controller that set the
// delay, the offset, the end and the loop of a sample, and the delay and the
// fade of an LFO, take the controllers 0 to 511 like every other opcode of
// a controller (release 0.3.2). With 512 a voice read behind the event
// vectors of the controllers.

#include <string>
#include <vector>

#include "AudioSpan.h"
#include "Region.h"
#include "Synth.h"

#include "aud_test.hpp"

// ############################################################################
// Opcodes of a controller

AUD_TEST(sfizz_region_ignores_the_opcodes_of_a_controller_out_of_range) {
  // The last controller, 511, and the one beyond it, 512: the region keeps
  // the first and ignores the second, and a note on it plays.
  for (int cc : {511, 512}) {
    const std::string n = std::to_string(cc);
    const std::string text = "<region> sample=*sine delay_oncc" + n + "=0.5 offset_oncc" + n +
                             "=100 end_oncc" + n + "=100 loop_start_oncc" + n +
                             "=10 loop_end_oncc" + n + "=10 lfo1_freq=1 lfo1_delay_oncc" + n +
                             "=0.5 lfo1_fade_oncc" + n + "=0.5\n";
    sfz::Synth synth;
    synth.setSamplesPerBlock(256);
    AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/region.sfz", text));
    AUD_CHECK(synth.getNumRegions() == 1);
    const sfz::Region* region = synth.getRegionView(0);
    const size_t kept = cc < 512 ? 1 : 0;
    AUD_CHECK(region->delayCC.size() == kept && region->delayCC.contains(cc) == (kept == 1));
    AUD_CHECK(region->offsetCC.size() == kept && region->offsetCC.contains(cc) == (kept == 1));
    AUD_CHECK(region->endCC.size() == kept && region->endCC.contains(cc) == (kept == 1));
    AUD_CHECK(region->loopStartCC.size() == kept);
    AUD_CHECK(region->loopEndCC.size() == kept);
    AUD_CHECK(region->lfos.size() == 1);
    if (region->lfos.size() == 1) {
      AUD_CHECK(region->lfos[0].delayCC.size() == kept);
      AUD_CHECK(region->lfos[0].fadeCC.size() == kept);
    }
    synth.noteOn(0, 60, 100);
    AUD_CHECK(synth.getNumActiveVoices() == 1);
    std::vector<float> left(256);
    std::vector<float> right(256);
    float* channels[2] = {left.data(), right.data()};
    synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, 256));
  }
}

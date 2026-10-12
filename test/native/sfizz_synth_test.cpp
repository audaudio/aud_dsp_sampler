// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the synth of the sfizz fork (src/sfizz/Synth.cpp) without the node:
// what a load keeps of the errors of its parser, and how it drops the
// regions whose samples it cannot read.

#include <string>
#include <vector>

#include "Config.h"
#include "Region.h"
#include "RegionSet.h"
#include "Synth.h"

#include "aud_test.hpp"

// ############################################################################
// Parse errors

AUD_TEST(sfizz_synth_keeps_the_first_errors_of_its_parser) {
  // An error on every line, like a large file that is no SFZ text.
  constexpr int kLines = 10000;
  std::string text;
  for (int i = 0; i < kLines; ++i) text += "garbage\n";
  sfz::Synth synth;
  AUD_CHECK(!synth.loadSfzString("/aud_dsp_sampler/garbage.sfz", text));
  const std::vector<std::string>& errors = synth.getParseErrors();
  AUD_CHECK(errors.size() == sfz::config::maxParseErrors);
  AUD_CHECK(!errors.empty() && errors.front().find("garbage.sfz at line 1: ") == 0);
}

// ############################################################################
// Regions a load drops

AUD_TEST(sfizz_synth_drops_a_region_from_every_list_that_points_at_it) {
  // In a group, regions of a sample that is not there with every kind of
  // keyswitch, and a sine that the last keyswitch 36 selects.
  const std::string text =
      "<group>\n"
      "<region> sample=gone.wav key=60 sw_last=36\n"
      "<region> sample=gone.wav key=60 sw_lolast=40 sw_hilast=41\n"
      "<region> sample=gone.wav key=60 sw_up=37\n"
      "<region> sample=gone.wav key=60 sw_down=38\n"
      "<region> sample=gone.wav key=60 sw_previous=39\n"
      "<region> sample=*sine key=61 sw_last=36\n";
  sfz::Synth synth;
  AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/keyswitches.sfz", text));
  AUD_CHECK(synth.getMissingSamples() == std::vector<std::string>{"gone.wav"});
  AUD_CHECK(synth.getNumRegions() == 1);
  // The group holds the region that is left.
  const std::vector<sfz::Region*>& grouped = synth.getRegionSetView(0)->getRegions();
  AUD_CHECK(std::vector<const sfz::Region*>(grouped.begin(), grouped.end()) ==
            std::vector<const sfz::Region*>{synth.getRegionView(0)});
  // The keyswitches walk the lists the parser filled. The last one stays
  // 36: the range 40 to 41 selects no region that is left.
  for (int key : {36, 37, 38, 39, 40, 41}) {
    synth.noteOn(0, key, 100);
    synth.noteOff(0, key, 0);
  }
  synth.noteOn(0, 61, 100);
  AUD_CHECK(synth.getNumActiveVoices() == 1);
}

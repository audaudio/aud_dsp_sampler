// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the synth of the sfizz fork (src/sfizz/Synth.cpp) without the node:
// what a load keeps of the errors of its parser, how it drops the regions
// whose samples it cannot read, and that an instrument without regions
// loads (ticket 27); that its own polyphony leaves out the voices a voice
// limit of the host stole (ticket 29); that it times its blocks only once
// the callback breakdown is asked for (ticket 30).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "AudioSpan.h"
#include "Config.h"
#include "Region.h"
#include "RegionSet.h"
#include "Synth.h"
#include "Voice.h"

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
// Instruments without regions

AUD_TEST(sfizz_synth_loads_an_instrument_without_regions) {
  // A comment, nothing at all, headers without a region: each loads, like
  // the empty text, and plays nothing.
  for (const char* text : {"// silent", "", "<control> set_cc64=127\n<global> volume=-6\n",
                           "<effect> type=gain gain=6\n"}) {
    sfz::Synth synth;
    synth.setSamplesPerBlock(256);
    AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/silent.sfz", text));
    AUD_CHECK(synth.getNumRegions() == 0);
    AUD_CHECK(synth.getParseErrors().empty());
    synth.noteOn(0, 60, 100);
    AUD_CHECK(synth.getNumActiveVoices() == 0);
    std::vector<float> left(256, 1.0f);
    std::vector<float> right(256, 1.0f);
    float* channels[2] = {left.data(), right.data()};
    synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, 256));
    float peak = 0;
    for (float sample : left) peak = std::max(peak, std::fabs(sample));
    AUD_CHECK(peak == 0.0f);
  }
  // A file that does not exist fails: its parser reports that it cannot
  // read it.
  sfz::Synth synth;
  AUD_CHECK(!synth.loadSfzFile("/aud_dsp_sampler/does-not-exist.sfz"));
  AUD_CHECK(!synth.getParseErrors().empty());
  // So does an include that does not exist, without a region beside it.
  AUD_CHECK(!synth.loadSfzString("/aud_dsp_sampler/include.sfz", "#include \"gone.sfz\"\n"));
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

// ############################################################################
// Voices a host steals (ticket 29)

AUD_TEST(sfizz_synth_polyphony_holds_beside_stolen_voices) {
  // A polyphony of two voices. A stolen voice frees its place at once: the
  // next note turns nothing off. The note after it exceeds the polyphony
  // and turns the oldest sounding voice off, as before; the stolen voice
  // is not taken for it.
  sfz::Synth synth;
  synth.setSamplesPerBlock(256);
  synth.setNumVoices(2);
  AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/sines.sfz",
                                "<region> sample=*sine ampeg_release=2\n"));
  synth.noteOn(0, 60, 100);
  synth.noteOn(0, 62, 100);
  AUD_CHECK(synth.stealVoices(1, 480).sounding == 1);
  synth.noteOn(0, 64, 100);
  AUD_CHECK(synth.countVoices().sounding == 2);
  AUD_CHECK(synth.countVoices().rendered == 3);
  synth.noteOn(0, 66, 100);
  AUD_CHECK(synth.countVoices().sounding == 2);
  AUD_CHECK(synth.countVoices().rendered == 4);
  int stolen = 0;
  for (const sfz::Voice* voice : synth.getActiveVoices()) stolen += voice->isStolen() ? 1 : 0;
  AUD_CHECK(stolen == 1);
}

// ############################################################################
// The callback breakdown (ticket 30)

AUD_TEST(sfizz_synth_times_its_blocks_once_the_breakdown_is_asked_for) {
  // Its clock reads cost a note-on a quarter of its time: a synth times its
  // blocks and events only once the breakdown was asked for, and never the
  // stages of a voice.
  sfz::Synth synth;
  synth.setSamplesPerBlock(256);
  AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/sines.sfz", "<region> sample=*sine\n"));
  std::vector<float> left(256);
  std::vector<float> right(256);
  float* channels[2] = {left.data(), right.data()};
  synth.noteOn(0, 60, 100);
  synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, 256));
  const sfz::Synth::CallbackBreakdown& breakdown = synth.getCallbackBreakdown();
  AUD_CHECK(breakdown.dispatch == 0 && breakdown.renderMethod == 0 && breakdown.effects == 0);
  synth.noteOn(0, 64, 100);
  synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, 256));
  AUD_CHECK(synth.getCallbackBreakdown().renderMethod > 0);
  AUD_CHECK(breakdown.data == 0 && breakdown.amplitude == 0 && breakdown.filters == 0 && breakdown.panning == 0);
}

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
// the callback breakdown is asked for (ticket 30); that a missing include
// fails a load and that note_offset_open_bounds keeps the outer keys under
// note_offset (ticket 31).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
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

// ############################################################################
// Includes that are missing (ticket 31)

AUD_TEST(sfizz_synth_fails_a_load_whose_include_is_missing) {
  // Beside a region that loads, an include that is not there fails the
  // load. The synth names it lexically normal and relative to the folder of
  // the SFZ file, the error names the whole path.
  sfz::Synth synth;
  AUD_CHECK(!synth.loadSfzString("/aud_dsp_sampler/presets/a/main.sfz",
                                 "<region> sample=*sine\n#include \"../../sets/x/../gone.sfz\"\n"));
  AUD_CHECK(synth.getMissingIncludes() == std::vector<std::string>{"../../sets/gone.sfz"});
  AUD_CHECK(!synth.getParseErrors().empty() &&
            synth.getParseErrors().front() ==
                "main.sfz at line 2: Cannot open file for reading: /aud_dsp_sampler/sets/gone.sfz");
  // A virtual path that no backend serves is missing too, named whole
  // below its own root.
  AUD_CHECK(!synth.loadSfzString("/aud_dsp_sampler/main.sfz",
                                 "<region> sample=*sine\n#include \"xy:/a/../b.sfz\"\n"));
  AUD_CHECK(synth.getMissingIncludes() == std::vector<std::string>{"xy:/b.sfz"});
  // The next load forgets them; a root file that cannot be read fails as
  // before and is no include.
  AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/main.sfz", "<region> sample=*sine\n"));
  AUD_CHECK(synth.getMissingIncludes().empty());
  AUD_CHECK(!synth.loadSfzFile("/aud_dsp_sampler/does-not-exist.sfz"));
  AUD_CHECK(synth.getMissingIncludes().empty());
}

// ############################################################################
// note_offset_open_bounds (ticket 31)

namespace {

// The key range and the key center of every region of a synth.
std::vector<std::vector<int>> keysOf(const sfz::Synth& synth) {
  std::vector<std::vector<int>> keys;
  for (int i = 0; i < synth.getNumRegions(); ++i) {
    const sfz::Region* region = synth.getRegionView(i);
    keys.push_back({region->keyRange.getStart(), region->keyRange.getEnd(),
                    region->pitchKeycenter});
  }
  return keys;
}

}  // namespace

AUD_TEST(sfizz_synth_note_offset_open_bounds_keeps_the_outer_keys) {
  // A region from key 0, one inside, one up to key 127, one without keys.
  const std::string regions =
      "<region> sample=*sine lokey=0 hikey=63 pitch_keycenter=12\n"
      "<region> sample=*sine lokey=30 hikey=40 pitch_keycenter=35\n"
      "<region> sample=*sine lokey=64 hikey=127 pitch_keycenter=115\n"
      "<region> sample=*sine pitch_keycenter=60\n";
  using Keys = std::vector<std::vector<int>>;
  const Keys none{{0, 63, 12}, {30, 40, 35}, {64, 127, 115}, {0, 127, 60}};
  const Keys down{{0, 51, 0}, {18, 28, 23}, {52, 115, 103}, {0, 127, 48}};
  const Keys downOpen{{0, 51, 0}, {18, 28, 23}, {52, 127, 103}, {0, 127, 48}};
  const Keys up{{12, 75, 24}, {42, 52, 47}, {76, 127, 127}, {0, 127, 72}};
  const Keys upOpen{{0, 75, 24}, {42, 52, 47}, {76, 127, 127}, {0, 127, 72}};
  // One synth for all: every load starts without the opcode.
  sfz::Synth synth;
  const std::vector<std::pair<std::string, Keys>> cases = {
      {"", none},
      {"note_offset_open_bounds=on", none},
      {"note_offset=-12", down},
      {"note_offset=-12 note_offset_open_bounds=on", downOpen},
      {"octave_offset=-1 note_offset_open_bounds=1", downOpen},
      {"note_offset=-12 note_offset_open_bounds=off", down},
      {"note_offset=-12", down},
      {"note_offset=12", up},
      {"note_offset_open_bounds=on note_offset=12", upOpen},
  };
  for (const auto& entry : cases) {
    AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/offset.sfz",
                                  "<control> " + entry.first + "\n" + regions));
    const Keys keys = keysOf(synth);
    AUD_CHECK(keys == entry.second);
    if (keys != entry.second) std::printf("  with <control> %s\n", entry.first.c_str());
  }
  // The keys the open bounds keep play: shifted by -12, key 120 plays the
  // region that ends at 127 only with the opcode.
  const std::string outer =
      "<region> sample=*sine lokey=0 hikey=63 pitch_keycenter=12\n"
      "<region> sample=*sine lokey=64 hikey=127 pitch_keycenter=115\n";
  for (bool open : {false, true}) {
    sfz::Synth player;
    player.setSamplesPerBlock(256);
    AUD_CHECK(player.loadSfzString(
        "/aud_dsp_sampler/outer.sfz",
        std::string("<control> note_offset=-12 note_offset_open_bounds=") + (open ? "on" : "off") +
            "\n" + outer));
    player.noteOn(0, 120, 100);
    AUD_CHECK(player.getNumActiveVoices() == (open ? 1 : 0));
    player.noteOn(0, 0, 100);
    AUD_CHECK(player.getNumActiveVoices() == (open ? 2 : 1));
  }
}

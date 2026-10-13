// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the voices of the sfizz fork (src/sfizz/Voice.cpp) on a synth of
// their own, without the node: a voice whose file the pool does not hold,
// and the release of a voice by the sustain pedal, by a note-off and when it
// is stolen, with the ADSR and with a flex EG for the amplitude (ticket 27).
//
// The pool of the synth reports its bytes to nobody. A test first lets the
// engine trim what the store holds for it, so that the store then holds only
// what the test adds, and frees that itself at its end.

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
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

// ############################################################################
// The release of a voice

namespace {

constexpr int kBlock = 256;
// The frame of the event that releases: block 40, 100 frames in.
constexpr int kEventBlock = 40;
constexpr int kEventOffset = 100;
constexpr long kEventFrame = static_cast<long>(kEventBlock) * kBlock + kEventOffset;

// Amplitude envelopes of instruments with linear stages: the ADSR, and
// the flex EG that replaces it, with the same attack and release.
std::string adsrOf(double release) {
  return "ampeg_attack=0.003 ampeg_release=" + std::to_string(release) + " ampeg_sustain=100";
}

std::string flexOf(double release) {
  const std::string r = std::to_string(release);
  return adsrOf(release) +
         " eg1_ampeg=1 eg1_sustain=1 eg1_time1=0.003 eg1_level1=1 eg1_shape1=0 eg1_time2=" + r +
         " eg1_level2=0 eg1_shape2=0";
}

// A synth that holds a constant of 0.5 on the keys of `opcodes`, so that
// its output is the amplitude envelope. Each block, `events` gets the synth
// and the block; it returns the output of channel 0 and, per block, the
// voices that sound.
struct Played {
  std::vector<float> left;
  std::vector<int> voices;
};

Played play(const std::string& opcodes, int blocks,
            const std::function<void(sfz::Synth&, int)>& events) {
  TempFolder folder;
  writeWav(folder.file("dc.wav"), std::vector<float>(48000, 0.5f));
  Played played;
  {
    sfz::Synth synth;
    synth.setSampleRate(48000);
    synth.setSamplesPerBlock(kBlock);
    AUD_CHECK(synth.loadSfzString(folder.file("held.sfz"),
                                  "<region> sample=dc.wav loop_mode=loop_continuous "
                                  "loop_start=0 loop_end=47999 " +
                                      opcodes + "\n"));
    std::vector<float> left(kBlock);
    std::vector<float> right(kBlock);
    float* channels[2] = {left.data(), right.data()};
    for (int block = 0; block < blocks; ++block) {
      events(synth, block);
      synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, kBlock));
      played.left.insert(played.left.end(), left.begin(), left.end());
      played.voices.push_back(synth.getNumActiveVoices());
    }
  }
  sfz::SampleStore::get().trim(std::numeric_limits<uint64_t>::max());
  return played;
}

// Note 60 from the first block on, released at kEventFrame by a note-off,
// or held by the pedal and released by the pedal going up to `pedalUp`.
Played release(const std::string& opcodes, bool pedal, int pedalUp = 0) {
  return play(opcodes, kEventBlock + 120, [&](sfz::Synth& synth, int block) {
    if (block == 0) {
      if (pedal) synth.cc(0, 64, 127);
      synth.noteOn(0, 60, 127);
    }
    if (pedal && block == kEventBlock / 2) synth.noteOff(0, 60, 0);
    if (block == kEventBlock) {
      if (pedal) {
        synth.cc(kEventOffset, 64, pedalUp);
      } else {
        synth.noteOff(kEventOffset, 60, 0);
      }
    }
  });
}

// The frames after kEventFrame until the voice is gone: the first block
// without a voice ends there.
long framesToEnd(const Played& played) {
  for (size_t block = kEventBlock; block < played.voices.size(); ++block) {
    if (played.voices[block] == 0) return static_cast<long>(block + 1) * kBlock - kEventFrame;
  }
  return -1;
}

}  // namespace

AUD_TEST(sfizz_voice_releases_on_the_pedal_going_up_like_on_a_note_off) {
  // A voice held by the pedal begins its release on the frame of the pedal
  // going up and ends like one a note-off releases there, with the ADSR and
  // with a flex EG for the amplitude.
  for (const std::string& opcodes : {adsrOf(0.2), flexOf(0.2)}) {
    const Played noteOff = release(opcodes, false);
    const Played pedal = release(opcodes, true);
    AUD_CHECK(noteOff.left.size() == pedal.left.size());
    float difference = 0;
    for (size_t i = 0; i < noteOff.left.size() && i < pedal.left.size(); ++i) {
      difference = std::max(difference, std::fabs(noteOff.left[i] - pedal.left[i]));
    }
    AUD_CHECK(difference == 0.0f);
    const float held = pedal.left[kEventFrame - 1];
    AUD_CHECK(held > 0.1f);
    AUD_CHECK(pedal.left[kEventFrame - 1] == pedal.left[kEventFrame - 2]);
    AUD_CHECK(pedal.left[kEventFrame + 1] < held);
    // The ADSR falls by 78 dB over its release, then fades out over 50 ms;
    // the flex EG falls to zero over its release. Each voice is gone with
    // the block that ends it.
    const long frames = framesToEnd(pedal);
    const long expected = opcodes == adsrOf(0.2) ? 9600 + 2400 : 9600;
    AUD_CHECK(frames >= expected && frames < expected + kBlock + 512);
  }
}

AUD_TEST(sfizz_voice_holds_while_the_pedal_is_at_1_or_above) {
  // sfizz counts the pedal as down from 1 of 127 on (sustain_lo=1): a pedal
  // that goes up slowly releases at 0. sustain_lo=64 releases below 64, as
  // MIDI 1.0 reads the switch.
  AUD_CHECK(framesToEnd(release(flexOf(0.05), true, 63)) == -1);
  AUD_CHECK(framesToEnd(release(flexOf(0.05), true, 1)) == -1);
  const long frames = framesToEnd(release(flexOf(0.05) + " sustain_lo=64", true, 63));
  AUD_CHECK(frames >= 2400 && frames < 2400 + kBlock + 512);
}

AUD_TEST(sfizz_voice_releases_on_the_pedal_without_the_sostenuto_switch) {
  // sostenuto_sw=off turns off the sostenuto pedal only: the sustain pedal
  // still releases its voices.
  for (const std::string& opcodes : {adsrOf(0.05), flexOf(0.05)}) {
    const long frames = framesToEnd(release(opcodes + " sostenuto_sw=off", true));
    AUD_CHECK(frames > 0 && frames < 2400 + 2400 + kBlock + 512);
  }
  // sustain_sw=off: the pedal holds nothing, the note-off releases.
  const Played free = release(flexOf(0.05) + " sustain_sw=off", true);
  AUD_CHECK(free.voices[kEventBlock / 2 + 12] == 0);
}

AUD_TEST(sfizz_voice_stolen_with_a_flex_eg_fades_quickly) {
  // A region of one voice: the second note steals the voice of the first,
  // which fades over off_time - 6 ms by default - instead of over its
  // release of a second, with the ADSR and with the flex EG. The ADSR then
  // fades out from -80 dB over 50 ms more.
  for (const std::string& opcodes : {adsrOf(1.0), flexOf(1.0)}) {
    for (const std::string& offMode : {std::string(), std::string(" off_mode=time off_time=0.1"),
                                       std::string(" off_mode=normal")}) {
      const Played played =
          play("polyphony=1 " + opcodes + offMode, kEventBlock + 60,
               [](sfz::Synth& synth, int block) {
                 if (block == 0) synth.noteOn(0, 60, 127);
                 if (block == kEventBlock) synth.noteOn(kEventOffset, 62, 127);
               });
      AUD_CHECK(played.voices[kEventBlock] == 2);
      if (offMode.empty()) {
        AUD_CHECK(played.voices[kEventBlock + 12] == 1);
        if (opcodes == flexOf(1.0)) AUD_CHECK(played.voices[kEventBlock + 2] == 1);
      } else if (offMode == " off_mode=time off_time=0.1") {
        AUD_CHECK(played.voices[kEventBlock + 15] == 2);
        AUD_CHECK(played.voices[kEventBlock + 45] == 1);
      } else {
        // off_mode=normal: the release of a second.
        AUD_CHECK(played.voices[kEventBlock + 59] == 2);
      }
    }
  }
}

AUD_TEST(sfizz_voice_turned_off_with_an_empty_flex_eg_ends_cleanly) {
  // eg1_ampeg=1 without points: the flex EG replaces the ADSR but has no
  // stage, so its voice is silent and ends with its first block. A voice
  // stolen or turned off fast in that block ends with it too, from any
  // event and any off mode, and the stealing voice does the same.
  for (const std::string& eg : {std::string("eg1_ampeg=1"), std::string("eg1_ampeg=1 eg1_dynamic=1"),
                                std::string("eg1_ampeg=1 eg1_sustain=0")}) {
    for (const std::string& offMode :
         {std::string(), std::string(" off_mode=fast"), std::string(" off_mode=time off_time=0"),
          std::string(" off_mode=time off_time=0.1"), std::string(" off_mode=normal")}) {
      const Played played = play("polyphony=1 " + eg + offMode, 4, [](sfz::Synth& synth, int block) {
        if (block != 0) return;
        synth.noteOn(0, 60, 127);
        synth.noteOn(kEventOffset, 62, 127);
        synth.noteOff(kEventOffset + 1, 62, 0);
      });
      AUD_CHECK(played.voices[0] == 0);
      AUD_CHECK(peak(played.left, 0, played.left.size()) == 0.0f);
    }
  }
}

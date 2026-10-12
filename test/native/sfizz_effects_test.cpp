// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the effects of the sfizz fork (src/sfizz/effects) on a synth of
// their own, without the node (ticket 27): the engine renders blocks of any
// size up to the largest, e.g. Android's callbacks of 64 to 960 frames, and
// every effect processes them as it processes whole blocks, without a
// failed check that writes to stderr on the audio thread.
//
// The instruments play sfizz's sine generator, so no file enters the store.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "AudioSpan.h"
#include "Synth.h"

#include "aud_test.hpp"

namespace {

constexpr int kMaxBlock = 256;
constexpr uint32_t kFrames = 9600;

// What a render left: both channels, and what sfizz wrote to stderr.
struct Render {
  std::vector<float> left;
  std::vector<float> right;
  std::string errors;
};

// Renders note 69 of `text` for kFrames frames in blocks whose sizes cycle
// through `blocks`, at most kMaxBlock, the block size of the synth.
Render render(const std::string& text, const std::vector<uint32_t>& blocks) {
  sfz::Synth synth;
  synth.setSampleRate(48000);
  synth.setSamplesPerBlock(kMaxBlock);
  AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/effects.sfz", text));
  synth.noteOn(0, 69, 100);
  std::vector<float> left(kMaxBlock);
  std::vector<float> right(kMaxBlock);
  float* channels[2] = {left.data(), right.data()};
  Render result;
  std::ostringstream captured;
  std::streambuf* const stderrBuffer = std::cerr.rdbuf(captured.rdbuf());
  size_t next = 0;
  for (uint32_t done = 0; done < kFrames;) {
    const uint32_t frames = std::min(blocks[next++ % blocks.size()], kFrames - done);
    synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, frames));
    result.left.insert(result.left.end(), left.begin(), left.begin() + frames);
    result.right.insert(result.right.end(), right.begin(), right.begin() + frames);
    done += frames;
  }
  std::cerr.rdbuf(stderrBuffer);
  result.errors = captured.str();
  return result;
}

float peakOf(const std::vector<float>& samples) {
  float result = 0;
  for (float sample : samples) result = std::max(result, std::fabs(sample));
  return result;
}

// The largest difference between two renders, on both channels.
float differenceOf(const Render& a, const Render& b) {
  float result = 0;
  for (size_t i = 0; i < a.left.size() && i < b.left.size(); ++i) {
    result = std::max(result, std::fabs(a.left[i] - b.left[i]));
    result = std::max(result, std::fabs(a.right[i] - b.right[i]));
  }
  return result;
}

// Short blocks of every size class the engine renders, and whole ones.
const std::vector<uint32_t> kShortBlocks = {1, 17, 64, 100, 255, 256, 3};
const std::vector<uint32_t> kWholeBlocks = {kMaxBlock};

const char* const kSine = "<region> sample=*sine\n";

}  // namespace

// ############################################################################
// Blocks shorter than the block size of the synth

AUD_TEST(sfizz_effects_process_blocks_shorter_than_the_block_size) {
  // Every effect type sfizz knows.
  const std::vector<std::string> effects = {
      "type=filter filter_type=lpf_2p filter_cutoff=2000",
      "type=eq eq_freq=1000 eq_gain=6",
      "type=apan apan_freq=2 apan_depth=50 apan_dry=50 apan_wet=50",
      "type=lofi bitred=50 decim=50",
      "type=limiter",
      "type=comp comp_threshold=-20 comp_ratio=8 comp_attack=0.001 comp_release=0.05",
      "type=comp comp_threshold=-20 comp_ratio=8 comp_stlink=on",
      "type=gate gate_threshold=-20",
      "type=gate gate_threshold=-20 gate_stlink=on",
      "type=disto disto_depth=50",
      "type=strings strings_number=12 strings_wet=50",
      "type=rectify rectify=50",
      "type=gain gain=6",
      "type=width width=50",
  };
  for (const std::string& effect : effects) {
    const std::string text = std::string(kSine) + "<effect> " + effect + "\n";
    const Render whole = render(text, kWholeBlocks);
    const Render shortBlocks = render(text, kShortBlocks);
    // No failed check of sfizz wrote to stderr.
    if (!whole.errors.empty() || !shortBlocks.errors.empty()) {
      std::printf("  %s: %s%s", effect.c_str(), whole.errors.c_str(),
                  shortBlocks.errors.c_str());
    }
    AUD_CHECK(whole.errors.empty());
    AUD_CHECK(shortBlocks.errors.empty());
    AUD_CHECK(peakOf(shortBlocks.left) > 0.0f);
  }
}

AUD_TEST(sfizz_effects_comp_and_gain_sound_the_same_in_any_blocks) {
  // The output stage of the instruments of audanika_app: a limiting
  // compressor, then a gain that undoes sfizz's volume of -7.35 dB.
  const std::string chain =
      "<effect> bus=main type=comp comp_attack=0 comp_release=0.05 comp_ratio=50 "
      "comp_threshold=-0.5 comp_stlink=0\n"
      "<effect> bus=main type=gain gain=7.35\n";
  const Render dry = render(kSine, kWholeBlocks);
  const Render whole = render(kSine + chain, kWholeBlocks);
  const Render shortBlocks = render(kSine + chain, kShortBlocks);
  AUD_CHECK(shortBlocks.errors.empty());
  AUD_CHECK(differenceOf(whole, shortBlocks) < 1e-6f);
  // Below the threshold the compressor passes the sine; the gain raises it
  // by 7.35 dB.
  AUD_CHECK_NEAR(peakOf(shortBlocks.left) / peakOf(dry.left), std::pow(10.0, 7.35 / 20), 0.01);
}

AUD_TEST(sfizz_effects_link_the_channels_on_every_frame) {
  // With the stereo link, the detector of the compressor and of the gate
  // follows both channels on every frame: the output does not depend on
  // where the blocks begin.
  for (const char* effect : {"type=comp comp_threshold=-30 comp_ratio=8 comp_attack=0.001 "
                             "comp_release=0.02 comp_stlink=on",
                             "type=gate gate_threshold=-12 gate_attack=0.001 "
                             "gate_release=0.02 gate_stlink=on"}) {
    const std::string text = std::string(kSine) + "<effect> " + effect + "\n";
    const Render whole = render(text, kWholeBlocks);
    const Render shortBlocks = render(text, kShortBlocks);
    AUD_CHECK(differenceOf(whole, shortBlocks) < 1e-6f);
  }
}

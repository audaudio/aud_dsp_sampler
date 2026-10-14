// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the gain effect of the sfizz fork (src/sfizz/effects/Gain.cpp)
// against its original (ticket 28): it converts its gain to a linear factor
// once, when it is made, where the original converted it for every sample,
// and gives the same output, bit for bit. A finite gain is at rest.

#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Defaults.h"
#include "Opcode.h"
#include "SIMDHelpers.h"
#include "effects/Gain.h"

#include "aud_test.hpp"

namespace {

constexpr int kFrames = 256;

// Gain::process of 0.3.0: the gain converted for every sample.
void originalGain(float gainDb, const float* const inputs[], float* const outputs[],
                  unsigned nframes) {
  std::vector<float> gains(nframes, gainDb);
  for (unsigned i = 0; i < nframes; ++i) gains[i] = std::pow(10.0f, 0.05f * gains[i]);
  for (unsigned c = 0; c < 2; ++c) {
    absl::Span<const float> input{inputs[c], nframes};
    absl::Span<float> output{outputs[c], nframes};
    sfz::applyGain(absl::Span<const float>{gains}, input, output);
  }
}

std::unique_ptr<sfz::fx::Gain> makeGain(const std::string& gainDb) {
  const std::vector<sfz::Opcode> opcodes = {{"type", "gain"}, {"gain", gainDb}};
  std::unique_ptr<sfz::Effect> effect = sfz::fx::Gain::makeInstance(opcodes);
  effect->setSampleRate(48000);
  effect->setSamplesPerBlock(kFrames);
  return std::unique_ptr<sfz::fx::Gain>(static_cast<sfz::fx::Gain*>(effect.release()));
}

// The gain in dB as the effect reads it from its opcode.
float readDb(const std::string& gainDb) {
  return sfz::Opcode("gain", gainDb).read(sfz::Default::volume);
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

}  // namespace

// ############################################################################
// process()

AUD_TEST(sfizz_gain_effect_gains_like_the_original) {
  // Noise in blocks, for gains up and down, 0 dB and the extremes: the bits
  // of the original.
  std::mt19937 random(28);
  std::normal_distribution<float> noise(0.0f, 0.5f);
  std::vector<float> left(16 * kFrames);
  std::vector<float> right(left.size());
  for (size_t i = 0; i < left.size(); ++i) {
    left[i] = noise(random);
    right[i] = noise(random);
  }
  for (const char* gainDb : {"7.35", "0", "-6", "-144", "48", "0.1"}) {
    const std::unique_ptr<sfz::fx::Gain> effect = makeGain(gainDb);
    size_t differing = 0;
    std::vector<float> out[2] = {std::vector<float>(kFrames), std::vector<float>(kFrames)};
    std::vector<float> expected[2] = {std::vector<float>(kFrames), std::vector<float>(kFrames)};
    for (size_t done = 0; done + kFrames <= left.size(); done += kFrames) {
      const float* inputs[2] = {left.data() + done, right.data() + done};
      float* outputs[2] = {out[0].data(), out[1].data()};
      float* expectedPointers[2] = {expected[0].data(), expected[1].data()};
      effect->process(inputs, outputs, kFrames);
      originalGain(readDb(gainDb), inputs, expectedPointers, kFrames);
      for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < kFrames; ++i) differing += sameBits(out[c][i], expected[c][i]) ? 0 : 1;
      }
    }
    AUD_CHECK(differing == 0);
  }
}

// ############################################################################
// linearGain()

AUD_TEST(sfizz_gain_effect_converts_its_gain_once_when_it_is_made) {
  // makeInstance converts the gain of the opcode, the factor of every
  // sample of the original; an effect without the opcode keeps 1.
  for (const char* gainDb : {"7.35", "0", "-6", "-144", "48", "0.1"}) {
    const std::unique_ptr<sfz::fx::Gain> effect = makeGain(gainDb);
    AUD_CHECK(sameBits(effect->linearGain(), std::pow(10.0f, 0.05f * readDb(gainDb))));
  }
  AUD_CHECK(sfz::fx::Gain().linearGain() == 1.0f);
  const std::vector<sfz::Opcode> none = {{"type", "gain"}};
  std::unique_ptr<sfz::Effect> plain = sfz::fx::Gain::makeInstance(none);
  AUD_CHECK(static_cast<sfz::fx::Gain&>(*plain).linearGain() == 1.0f);
}

// ############################################################################
// isAtRest()

AUD_TEST(sfizz_gain_effect_is_at_rest_with_a_finite_factor) {
  // A finite factor keeps silence silent; a gain the opcode allows beyond
  // its range, so large that its factor overflows and silence would become
  // NaN, is not at rest.
  AUD_CHECK(makeGain("7.35")->isAtRest());
  AUD_CHECK(makeGain("-144")->isAtRest());
  const std::unique_ptr<sfz::fx::Gain> huge = makeGain("1000");
  AUD_CHECK(std::isinf(huge->linearGain()));
  AUD_CHECK(!huge->isAtRest());
}

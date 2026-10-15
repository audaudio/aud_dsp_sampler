// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the helpers ticket 30 added to the SIMD helpers of the sfizz fork
// (src/sfizz/SIMDHelpers.h and SIMDHelpers.cpp). The sum of a step,
// stepSum(), which the positions of a voice and the time of its flex EG
// take, gives what a loop of float additions gives, bit for bit - across
// binades, with a step that rounds to nothing, with steps halfway between
// two multiples of an ulp, from 0, for the sample period of 48 kHz and for
// the steps of a voice's pitch. The split of a position, splitPosition(),
// which the loops of a voice and sfzInterpolationCast() take, gives what
// the cast of sfizz gave before.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "SIMDHelpers.h"

#include "aud_test.hpp"

namespace {

// What stepSum() must give: the loop.
std::vector<float> loopSum(float start, float step, unsigned size) {
  std::vector<float> result(size);
  if (size == 0) return result;
  result[0] = start;
  for (unsigned i = 1; i < size; ++i) result[i] = result[i - 1] + step;
  return result;
}

bool sumsAlike(float start, float step, unsigned size) {
  const std::vector<float> expected = loopSum(start, step, size);
  std::vector<float> actual(size + 1, -1.0f);
  sfz::stepSum(start, step, actual.data(), size);
  // Nothing past the frames.
  if (actual[size] != -1.0f) return false;
  return std::memcmp(expected.data(), actual.data(), size * sizeof(float)) == 0;
}

// A float from its bits.
float fromBits(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// What splitPosition() must give: the cast of sfizz before ticket 30
// (_internals::snippetSFZInterpolationCast()), the start added after it.
bool splitsAlike(float position, int start) {
  constexpr float maxJump{1 << 24};
  const float limitedJump = ::min(maxJump, position);
  const int expectedIndex = static_cast<int>(limitedJump) + start;
  const float expectedCoeff = limitedJump - static_cast<float>(static_cast<int>(limitedJump));
  int index = -1;
  float coeff = -1.0f;
  sfz::splitPosition(position, start, index, coeff);
  return index == expectedIndex && std::memcmp(&coeff, &expectedCoeff, sizeof(coeff)) == 0;
}

}  // namespace

AUD_TEST(sfizz_step_sum_sums_as_a_loop_bit_for_bit) {
  const float period = 1.0f / 48000.0f;
  // The time of an EG from the start of a stage and late in it.
  for (float start : {0.0f, period, 0.003f, 0.2499f, 0.29f, 1.7f, 31.9f}) {
    for (unsigned size : {0u, 1u, 2u, 7u, 180u, 1024u}) AUD_CHECK(sumsAlike(start, period, size));
  }
  // The positions of a voice: a start within a frame, steps of its pitch
  // ratio, beyond 1 for notes above the root.
  for (float step : {0.25f, 0.5f, 0.9438743f, 1.0f, 1.0594631f, 1.4983071f, 2.0f, 3.1748021f, 7.9f}) {
    for (float start : {0.0f, 0.37f, 0.9999999f, 1.0594631f}) AUD_CHECK(sumsAlike(start, step, 180));
  }
  // Steps that round to nothing, and steps of every size from the bits of
  // a pseudo-random sequence, some of them halfway between multiples of an
  // ulp of the sums.
  AUD_CHECK(sumsAlike(4096.0f, 1e-4f, 300));
  AUD_CHECK(sumsAlike(1.0f, std::ldexp(1.0f, -24), 300));
  AUD_CHECK(sumsAlike(1.0f, 3.0f * std::ldexp(1.0f, -24), 300));
  AUD_CHECK(sumsAlike(3.0f, std::ldexp(1.0f, -23), 300));
  uint32_t state = 12345;
  for (int round = 0; round < 2000; ++round) {
    state = state * 1664525u + 1013904223u;
    // Exponents of steps from 2^-20 to 2^4, starts from 0 to 2^8.
    const float step = fromBits(((107u + (state >> 27) % 24u) << 23) | (state & 0x7fffffu));
    state = state * 1664525u + 1013904223u;
    const float start = (state >> 9) * std::ldexp(1.0f, -15);
    AUD_CHECK(sumsAlike(start, step, 181));
    // A step of few significant bits, which ties more often.
    const float coarse = std::ldexp(static_cast<float>(1 + (state >> 28)), -static_cast<int>((state >> 20) % 24));
    AUD_CHECK(sumsAlike(start, coarse, 181));
  }
  // What is no normal positive sum goes the way of the loop.
  AUD_CHECK(sumsAlike(-5.0f, 0.25f, 100));
  AUD_CHECK(sumsAlike(0.0f, std::numeric_limits<float>::denorm_min(), 100));
  AUD_CHECK(sumsAlike(std::numeric_limits<float>::max() / 2, std::numeric_limits<float>::max() / 8, 20));
}

AUD_TEST(sfizz_split_position_splits_as_the_cast_before) {
  // Positions within a frame, on a frame, below 1, of many frames, where a
  // float holds few fractional bits, and beyond 2^24, which counts as 2^24.
  for (float position : {0.0f, 0.37f, 0.9999999f, 1.0f, 1.0594631f, 47.5f, 47999.99f, 1048575.9f, 8388607.5f,
                         16777215.0f, 16777216.0f, 16777218.0f, 1e9f}) {
    for (int start : {0, 1, 48000, -3}) AUD_CHECK(splitsAlike(position, start));
  }
  // Positions of every size from the bits of a pseudo-random sequence.
  uint32_t state = 54321;
  for (int round = 0; round < 10000; ++round) {
    state = state * 1664525u + 1013904223u;
    // Exponents from 2^-20 to 2^27.
    const float position = fromBits(((107u + (state >> 26) % 48u) << 23) | (state & 0x7fffffu));
    AUD_CHECK(splitsAlike(position, static_cast<int>(state >> 24)));
  }
}

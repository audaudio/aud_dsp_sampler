// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the helpers of the sfizz fork in src/sfizz/MathHelpers.h:
// SameArgumentCache, through which a voice converts its pitch and its
// volume modulation once per run of equal values instead of once per sample
// (ticket 28), gives the results of the calls it saves, bit for bit.

#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "MathHelpers.h"
#include "SfzHelpers.h"

#include "aud_test.hpp"

// ############################################################################
// SameArgumentCache

AUD_TEST(sfizz_same_argument_cache_gives_the_results_of_every_call) {
  // Runs of equal arguments, both zeros, NaN and denormals: the cached
  // conversions give the bits the calls give.
  std::mt19937 random(28);
  std::uniform_real_distribution<float> cents(-2400.0f, 2400.0f);
  std::uniform_int_distribution<int> runs(1, 40);
  std::vector<float> arguments;
  while (arguments.size() < 100000) {
    float value = cents(random);
    switch (arguments.size() % 7) {
      case 0:
        value = 0.0f;
        break;
      case 1:
        value = -0.0f;
        break;
      case 2:
        value = std::numeric_limits<float>::quiet_NaN();
        break;
      case 3:
        value = std::numeric_limits<float>::denorm_min();
        break;
      default:
        break;
    }
    arguments.insert(arguments.end(), runs(random), value);
  }
  auto ratioOfCents = cacheSameArgument([](float c) { return sfz::centsFactor(c); });
  auto gainOfDb = cacheSameArgument([](float dB) { return db2mag(dB); });
  size_t differing = 0;
  for (float argument : arguments) {
    const float ratio = ratioOfCents(argument);
    const float expectedRatio = sfz::centsFactor(argument);
    const float gain = gainOfDb(argument * 0.01f);
    const float expectedGain = db2mag(argument * 0.01f);
    differing += std::memcmp(&ratio, &expectedRatio, sizeof(float)) == 0 ? 0 : 1;
    differing += std::memcmp(&gain, &expectedGain, sizeof(float)) == 0 ? 0 : 1;
  }
  AUD_CHECK(differing == 0);
}

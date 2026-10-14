// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the oversampling filters of hiir in the sfizz fork
// (src/external/hiir/hiir/Upsampler2x*.h, Downsampler2x*.h), with which
// the compressor runs at twice the rate: is_cleared() tells whether their
// memory is silent (ticket 28), and settle() clears a memory that lies
// within a floor of 0 (release 0.3.2), so that the compressor comes to rest
// soon after a sound. The FPU filters build on every platform, the SIMD
// filters of the platform - NEON or SSE - through sfizz's choice.

#include <algorithm>
#include <cmath>
#include <limits>

#include <hiir/Downsampler2xFpu.h>
#include <hiir/Upsampler2xFpu.h>

#include "OversamplerHelpers.h"

#include "aud_test.hpp"

namespace {

// One input sample through an upsampler; the larger of its two outputs.
template <class Upsampler>
float upsample(Upsampler& filter, float input) {
  float first = 0;
  float second = 0;
  filter.process_sample(first, second, input);
  return std::max(std::fabs(first), std::fabs(second));
}

// Two input samples through a downsampler; its output.
template <class Downsampler>
float downsample(Downsampler& filter, float input) {
  const float pair[2] = {input, 0.0f};
  return std::fabs(filter.process_sample(pair));
}

// An impulse, then silence: settle() keeps the memory while it lies above
// the floor, clears it once it lies within, and what the filter gives of
// the silence after that stays far below the floor. A floor of 0 keeps any
// memory but silence, and a memory that is not a number stays.
template <class Filter, class Step>
void checkSettle(Step step) {
  Filter filter;
  filter.set_coefs(sfz::OSCoeffs2x);
  filter.clear_buffers();
  AUD_CHECK(filter.is_cleared());
  step(filter, 1.0f);
  AUD_CHECK(!filter.is_cleared());
  Filter copy = filter;
  copy.settle(1e-9f);
  AUD_CHECK(!copy.is_cleared());
  int steps = 0;
  bool cleared = false;
  bool clearedAtZero = false;
  while (!cleared && steps < 100000) {
    step(filter, 0.0f);
    steps += 1;
    copy = filter;
    copy.settle(0.0f);
    clearedAtZero = clearedAtZero || copy.is_cleared();
    copy.settle(1e-9f);
    cleared = copy.is_cleared();
  }
  AUD_CHECK(cleared);
  AUD_CHECK(!clearedAtZero);
  float peak = 0;
  for (int i = 0; i < 10000; ++i) peak = std::max(peak, step(filter, 0.0f));
  AUD_CHECK(peak < 1e-8f);

  Filter broken;
  broken.set_coefs(sfz::OSCoeffs2x);
  broken.clear_buffers();
  step(broken, std::numeric_limits<float>::quiet_NaN());
  broken.settle(1e30f);
  AUD_CHECK(!broken.is_cleared());
}

}  // namespace

// ############################################################################
// settle(floor)

AUD_TEST(sfizz_hiir_filters_settle_a_memory_within_the_floor) {
  checkSettle<hiir::Upsampler2xFpu<12>>(upsample<hiir::Upsampler2xFpu<12>>);
  checkSettle<hiir::Downsampler2xFpu<12>>(downsample<hiir::Downsampler2xFpu<12>>);
  checkSettle<hiir::Upsampler2x<12>>(upsample<hiir::Upsampler2x<12>>);
  checkSettle<hiir::Downsampler2x<12>>(downsample<hiir::Downsampler2x<12>>);
}

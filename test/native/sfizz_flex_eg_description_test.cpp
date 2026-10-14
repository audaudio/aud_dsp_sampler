// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the shapes of the flex EGs of the sfizz fork
// (src/sfizz/FlexEGDescription.cpp): the points of one shape share its
// curve, which the process holds only while a point uses it, and synths that
// load at the same time on several threads share the curves without a race
// (ticket 28). Run alone, the parallel loads build the first synths of the
// process, as the loads of an app that starts do.

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Curve.h"
#include "FlexEGDescription.h"
#include "Region.h"
#include "Synth.h"

#include "aud_test.hpp"

namespace {

// The value of the curve of `shape` at x = 64 / 127: x^(1 + shape) for a
// positive shape, 1 - (1 - x)^(1 - shape) for a negative one.
double shapeAt64(double shape) {
  const double x = 64.0 / 127.0;
  return shape >= 0 ? std::pow(x, 1 + shape) : 1 - std::pow(1 - x, 1 - shape);
}

// An instrument whose flex EG has three points: `own` and `shared` are the
// shapes of its first two stages, the third is linear.
std::string flexInstrument(float own, float shared) {
  return "<region> sample=*sine eg1_ampeg=1 eg1_sustain=2 eg1_time1=0.01 eg1_level1=1 "
         "eg1_shape1=" + std::to_string(own) +
         " eg1_time2=0.1 eg1_level2=0.5 eg1_shape2=" + std::to_string(shared) +
         " eg1_time3=0.2 eg1_level3=0\n";
}

// Whether the curves of the loaded instrument have the shapes it named.
bool hasShapes(const sfz::Synth& synth, float own, float shared) {
  const sfz::Region* region = synth.getRegionView(0);
  if (region == nullptr || region->flexEGs.empty()) return false;
  const std::vector<sfz::FlexEGPoint>& points = region->flexEGs[0].points;
  if (points.size() != 4) return false;
  return std::fabs(points[1].curve().evalCC7(64) - shapeAt64(own)) < 1e-5 &&
         std::fabs(points[2].curve().evalCC7(64) - shapeAt64(shared)) < 1e-5;
}

}  // namespace

// ############################################################################
// Shapes

AUD_TEST(sfizz_flex_eg_points_share_the_curve_of_a_shape) {
  std::weak_ptr<sfz::Curve> held;
  {
    sfz::FlexEGPoint first;
    sfz::FlexEGPoint second;
    first.setShape(3.25f);
    second.setShape(3.25f);
    AUD_CHECK(&first.curve() == &second.curve());
    AUD_CHECK_NEAR(first.curve().evalCC7(64), shapeAt64(3.25), 1e-6);
    held = sfz::FlexEGs::getShapeCurve(3.25f);
    AUD_CHECK(held.lock().get() == &first.curve());
    // A negative shape bends the other way; the shape 0 is linear.
    second.setShape(-1.5f);
    AUD_CHECK_NEAR(second.curve().evalCC7(64), shapeAt64(-1.5), 1e-6);
    second.setShape(0.0f);
    AUD_CHECK_NEAR(second.curve().evalCC7(64), 64.0 / 127.0, 1e-6);
  }
  // The process holds a curve only while a point uses it.
  AUD_CHECK(held.expired());
  sfz::FlexEGs::clearUnusedCurves();
  const std::shared_ptr<sfz::Curve> again = sfz::FlexEGs::getShapeCurve(3.25f);
  AUD_CHECK_NEAR(again->evalCC7(64), shapeAt64(3.25), 1e-6);
}

// ############################################################################
// Loads on several threads at once

AUD_TEST(sfizz_flex_eg_shapes_load_on_several_threads_at_once) {
  // Every thread loads instruments with a shape of its own on every load
  // and one that all threads share; each load drops the curves no point
  // uses. Without the lock of the shapes, the threads corrupted the map of
  // the process (ThreadSanitizer: a data race; on a phone: a free of a
  // pointer that was not allocated).
  constexpr int kThreads = 4;
  constexpr int kLoads = 100;
  constexpr float kShared = -2.0f;
  std::atomic<bool> start{false};
  std::vector<int> loaded(kThreads, 0);
  std::vector<int> shaped(kThreads, 0);
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start.load()) std::this_thread::yield();
      sfz::Synth synth;
      for (int i = 0; i < kLoads; ++i) {
        const float own = 0.5f + static_cast<float>(t) + 0.0078125f * static_cast<float>(i);
        if (!synth.loadSfzString("/aud_dsp_sampler/flex.sfz", flexInstrument(own, kShared))) {
          continue;
        }
        loaded[t] += 1;
        if (hasShapes(synth, own, kShared)) shaped[t] += 1;
      }
    });
  }
  start.store(true);
  for (std::thread& thread : threads) thread.join();
  AUD_CHECK(loaded == std::vector<int>(kThreads, kLoads));
  AUD_CHECK(shaped == std::vector<int>(kThreads, kLoads));
}

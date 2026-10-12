// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the flush-to-zero scope of the sfizz fork (src/sfizz/ScopedFTZ.cpp):
// it sets the flag of the floating-point control register for its scope and
// gives the register back afterwards, so that a synth that renders on the
// engine's audio thread leaves flush-to-zero off for the other nodes there.

#include <cstdint>
#include <memory>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <pmmintrin.h>
#endif

#include "AudioSpan.h"
#include "ScopedFTZ.h"
#include "Synth.h"

#include "aud_test.hpp"

namespace {

// The floating-point control register of the CPU and its flush-to-zero
// bits: FPCR.FZ on arm64, MXCSR.FTZ and MXCSR.DAZ on x86.
#if defined(__aarch64__)
constexpr uint64_t kFlushToZero = uint64_t{1} << 24;

uint64_t controlRegister() {
  uint64_t value;
  asm volatile("mrs %0, fpcr" : "=r"(value));
  return value;
}

void setControlRegister(uint64_t value) { asm volatile("msr fpcr, %0" : : "r"(value)); }
#elif defined(__x86_64__) || defined(__i386__)
constexpr uint64_t kFlushToZero = _MM_FLUSH_ZERO_MASK | _MM_DENORMALS_ZERO_MASK;

uint64_t controlRegister() { return _mm_getcsr(); }

void setControlRegister(uint64_t value) { _mm_setcsr(static_cast<unsigned>(value)); }
#else
constexpr uint64_t kFlushToZero = 0;

uint64_t controlRegister() { return 0; }

void setControlRegister(uint64_t) {}
#endif

// The register as the test thread had it, with flush-to-zero off; restores
// it at the end of a test.
struct RegisterScope {
  const uint64_t before = controlRegister();
  RegisterScope() { setControlRegister(before & ~kFlushToZero); }
  ~RegisterScope() { setControlRegister(before); }
};

}  // namespace

// ############################################################################
// ScopedFTZ

AUD_TEST(sfizz_scoped_ftz_flushes_to_zero_for_its_scope_only) {
  RegisterScope scope;
  const uint64_t outside = controlRegister();
  {
    ScopedFTZ ftz;
    AUD_CHECK((controlRegister() & kFlushToZero) == kFlushToZero);
  }
  AUD_CHECK(controlRegister() == outside);
}

AUD_TEST(sfizz_synth_leaves_the_control_register_as_it_found_it) {
  RegisterScope scope;
  const uint64_t outside = controlRegister();
  auto synth = std::make_unique<sfz::Synth>();
  synth->setSamplesPerBlock(256);
  std::vector<float> left(256);
  std::vector<float> right(256);
  float* channels[2] = {left.data(), right.data()};
  synth->renderBlock(sfz::AudioSpan<float>(channels, 2, 0, 256));
  AUD_CHECK(controlRegister() == outside);
}

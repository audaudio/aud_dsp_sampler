// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the pan of one pass of the sfizz fork (src/sfizz/Panning.cpp,
// ticket 30): panMono() and panStereo() give what the passes they replace
// give, bit for bit - the gain of the voice, the pan, the width, the
// position and the compensation one after the other, over envelopes from
// aligned buffers as the buffer pool hands them out. Also for blocks whose
// size is no multiple of 4 and for channels that do not start aligned, where
// the NEON code of pan() and width() leaves frames to its scalar code.

#include <cstring>
#include <initializer_list>

#include "Buffer.h"
#include "Panning.h"
#include "SIMDHelpers.h"

#include "aud_test.hpp"

namespace {

constexpr unsigned kMost = 260;

// Values of a signal or an envelope, from a seed.
void fillValues(float* values, unsigned size, unsigned seed) {
  uint32_t state = seed * 2654435761u + 1;
  for (unsigned i = 0; i < size; ++i) {
    state = state * 1664525u + 1013904223u;
    values[i] = static_cast<float>(static_cast<int32_t>(state >> 8) - (1 << 23)) / static_cast<float>(1 << 23);
  }
}

// An aligned buffer holding `value` at every frame, as a stage fills its
// envelope from the pool.
sfz::Buffer<float> envelopeOf(float value, unsigned size) {
  sfz::Buffer<float> buffer(size);
  sfz::fill<float>(absl::MakeSpan(buffer.data(), size), value);
  return buffer;
}

bool sameBits(const float* a, const float* b, unsigned size) {
  return std::memcmp(a, b, size * sizeof(float)) == 0;
}

}  // namespace

AUD_TEST(sfizz_panning_mono_in_one_pass_gives_the_passes_bit_for_bit) {
  sfz::initializeSIMDDispatchers();
  const float gain = 1.4125375446227544f;
  for (unsigned size : {1u, 3u, 4u, 7u, 180u, 181u, 256u}) {
    for (unsigned offset : {0u, 1u}) {
      for (float pan : {-1.0f, -0.3f, 0.0f, 0.25f, 0.999f, 1.0f}) {
        sfz::Buffer<float> envelope(kMost);
        fillValues(envelope.data(), size, size + offset);
        sfz::Buffer<float> channels[4] = {sfz::Buffer<float>(kMost), sfz::Buffer<float>(kMost),
                                          sfz::Buffer<float>(kMost), sfz::Buffer<float>(kMost)};
        float* left = channels[0].data() + offset;
        float* right = channels[1].data() + offset;
        float* oneLeft = channels[2].data() + offset;
        float* oneRight = channels[3].data() + offset;
        fillValues(left, size, 7 * size);
        std::memcpy(oneLeft, left, size * sizeof(float));

        // The passes of Voice::Impl::ampStageMono() and panStageMono().
        sfz::applyGain<float>(envelope.data(), left, left, size);
        sfz::copy<float>(left, right, size);
        sfz::Buffer<float> panEnvelope = envelopeOf(pan, size);
        sfz::pan(panEnvelope.data(), left, right, size);
        sfz::applyGain1<float>(gain, left, left, size);
        sfz::applyGain1<float>(gain, right, right, size);

        sfz::panMono(pan, gain, envelope.data(), oneLeft, oneRight, size);
        AUD_CHECK(sameBits(left, oneLeft, size));
        AUD_CHECK(sameBits(right, oneRight, size));
      }
    }
  }
}

AUD_TEST(sfizz_panning_stereo_in_one_pass_gives_the_passes_bit_for_bit) {
  sfz::initializeSIMDDispatchers();
  const float gain = 1.9952623149688797f;
  struct Stage {
    float pan;
    float width;
    float position;
  };
  // The defaults - a width of 1 is no identity in the table - and others.
  const Stage stages[] = {{0.0f, 1.0f, 0.0f}, {0.1f, 0.5f, -0.3f}, {-1.0f, -1.0f, 1.0f}, {0.4f, 0.0f, 0.2f}};
  for (unsigned size : {1u, 3u, 4u, 7u, 180u, 181u, 256u}) {
    for (unsigned offset : {0u, 1u}) {
      for (const Stage& stage : stages) {
        sfz::Buffer<float> envelope(kMost);
        fillValues(envelope.data(), size, size + offset);
        sfz::Buffer<float> channels[4] = {sfz::Buffer<float>(kMost), sfz::Buffer<float>(kMost),
                                          sfz::Buffer<float>(kMost), sfz::Buffer<float>(kMost)};
        float* left = channels[0].data() + offset;
        float* right = channels[1].data() + offset;
        float* oneLeft = channels[2].data() + offset;
        float* oneRight = channels[3].data() + offset;
        fillValues(left, size, 3 * size);
        fillValues(right, size, 5 * size + 1);
        std::memcpy(oneLeft, left, size * sizeof(float));
        std::memcpy(oneRight, right, size * sizeof(float));

        // The passes of Voice::Impl::ampStageStereo() and panStageStereo().
        sfz::applyGain<float>(envelope.data(), left, left, size);
        sfz::applyGain<float>(envelope.data(), right, right, size);
        sfz::Buffer<float> panEnvelope = envelopeOf(stage.pan, size);
        sfz::pan(panEnvelope.data(), left, right, size);
        sfz::Buffer<float> widthEnvelope = envelopeOf(stage.width, size);
        sfz::width(widthEnvelope.data(), left, right, size);
        sfz::Buffer<float> positionEnvelope = envelopeOf(stage.position, size);
        sfz::pan(positionEnvelope.data(), left, right, size);
        sfz::applyGain1<float>(gain, left, left, size);
        sfz::applyGain1<float>(gain, right, right, size);

        sfz::panStereo(stage.pan, stage.width, stage.position, gain, envelope.data(), oneLeft, oneRight,
                       size);
        AUD_CHECK(sameBits(left, oneLeft, size));
        AUD_CHECK(sameBits(right, oneRight, size));
      }
    }
  }
}

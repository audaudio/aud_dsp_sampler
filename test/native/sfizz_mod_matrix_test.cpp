// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the modulation matrix of the sfizz fork
// (src/sfizz/modulations/ModMatrix.cpp, ticket 30), which hands out the
// buffer of a target's single source at depth 1 instead of a copy of it:
// what a target hands out and what a source reads from it stay what sfizz
// gave before, bit for bit.
//
// - A further source adds to the first or multiplies it as the scalar loop
//   does, also when it reads the target while it generates: it finds the
//   first source there, which sfizz copied into the target's buffer before
//   it generated the further one.
// - A source that reads its own target while it generates - a cycle -
//   finds the target's output of the block before, which the target's
//   buffer held, and the target hands out its output in that buffer.
// - Instruments whose targets take a single source at depth 1 render the
//   same as with a further source that changes no bit, with which the
//   target copies its first source as sfizz did before.
//
// The cycles take generators of the test: no opcode connects one. The
// frequency of an LFO, which the LFO reads while it generates, takes CCs
// and flex EGs, and neither of them reads a target.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "AudioSpan.h"
#include "Synth.h"
#include "modulations/ModGenerator.h"
#include "modulations/ModId.h"
#include "modulations/ModKey.h"
#include "modulations/ModMatrix.h"

#include "aud_test.hpp"

namespace {

bool sameBits(const float* a, const float* b, size_t size) {
  return std::memcmp(a, b, size * sizeof(float)) == 0;
}

// ############################################################################
// A matrix with sources of the test

// A block that is no multiple of the 4 frames of a vector.
constexpr unsigned kFrames = 61;
constexpr int kBlocks = 5;

const NumericId<sfz::Region> kRegion{0};
const NumericId<sfz::Voice> kVoice{0};

// A source of pseudo-random values, new ones in every block. It may read a
// target while it generates, before it writes, as an LFO reads its
// frequency. It keeps what it read and what it wrote, and notes in `order`
// when it generates.
class TestSource : public sfz::ModGenerator {
 public:
  TestSource(sfz::ModMatrix& matrix, uint32_t seed, std::vector<const TestSource*>& order)
      : matrix_(matrix), state_(seed), order_(order) {}

  void readWhileGenerating(sfz::ModMatrix::TargetId target) { target_ = target; }

  void init(const sfz::ModKey&, NumericId<sfz::Voice>, unsigned) override {}

  void generate(const sfz::ModKey&, NumericId<sfz::Voice>, absl::Span<float> buffer) override {
    order_.push_back(this);
    if (target_.valid()) {
      read = matrix_.getModulation(target_);
      if (read != nullptr) seen.assign(read, read + buffer.size());
    }
    for (float& value : buffer) {
      state_ = state_ * 1664525u + 1013904223u;
      // From -2 to 2 with every bit of the mantissa, so that sums and
      // products round.
      value = static_cast<float>(static_cast<int32_t>(state_ >> 8) - (1 << 23)) / static_cast<float>(1 << 22);
    }
    output.assign(buffer.begin(), buffer.end());
  }

  // The buffer it read the target from, what it found there, and what it
  // wrote, of the last block.
  const float* read = nullptr;
  std::vector<float> seen;
  std::vector<float> output;

 private:
  sfz::ModMatrix& matrix_;
  uint32_t state_;
  std::vector<const TestSource*>& order_;
  sfz::ModMatrix::TargetId target_;
};

sfz::ModKey lfoKey(uint8_t number) { return sfz::ModKey::createNXYZ(sfz::ModId::LFO, kRegion, number); }

sfz::ModKey targetKey(sfz::ModId id) { return sfz::ModKey::createNXYZ(id, kRegion); }

// Connects `source` to `target` at depth 1, without a modulation of the
// depth.
bool connect(sfz::ModMatrix& matrix, sfz::ModMatrix::SourceId source, sfz::ModMatrix::TargetId target) {
  return matrix.connect(source, target, 1.0f, sfz::ModKey(), 0.0f);
}

// Starts the voice of the region, as a synth does after a load.
void startVoice(sfz::ModMatrix& matrix) {
  matrix.init();
  matrix.initVoice(kVoice, kRegion, 0);
}

void beginBlock(sfz::ModMatrix& matrix) {
  matrix.beginCycle(kFrames);
  matrix.beginVoice(kVoice, kRegion, 1.0f);
}

void endBlock(sfz::ModMatrix& matrix) {
  matrix.endVoice();
  matrix.endCycle();
}

}  // namespace

AUD_TEST(sfizz_mod_matrix_sums_and_multiplies_two_sources_as_the_scalar_loop) {
  sfz::ModMatrix matrix;
  matrix.setSamplesPerBlock(kFrames);
  std::vector<const TestSource*> order;
  TestSource a(matrix, 1, order);
  TestSource b(matrix, 2, order);
  const auto sourceA = matrix.registerSource(lfoKey(0), a);
  const auto sourceB = matrix.registerSource(lfoKey(1), b);
  // An additive and a multiplicative target of both sources, and a target
  // of the first only.
  const auto pitch = matrix.registerTarget(targetKey(sfz::ModId::Pitch));
  const auto amplitude = matrix.registerTarget(targetKey(sfz::ModId::Amplitude));
  const auto volume = matrix.registerTarget(targetKey(sfz::ModId::Volume));
  for (auto target : {pitch, amplitude}) {
    AUD_CHECK(connect(matrix, sourceA, target));
    AUD_CHECK(connect(matrix, sourceB, target));
  }
  AUD_CHECK(connect(matrix, sourceA, volume));
  startVoice(matrix);
  for (int block = 0; block < kBlocks; ++block) {
    beginBlock(matrix);
    const float* sum = matrix.getModulation(pitch);
    const float* product = matrix.getModulation(amplitude);
    const float* single = matrix.getModulation(volume);
    AUD_CHECK(sum != nullptr && product != nullptr && single != nullptr);
    if (sum == nullptr || product == nullptr || single == nullptr) return;
    // A product with the depth of 1 is exact, and a sum or a product of
    // two is the same in either order: whichever source the matrix takes
    // first, sfizz gave these.
    std::vector<float> sums(kFrames);
    std::vector<float> products(kFrames);
    for (unsigned i = 0; i < kFrames; ++i) {
      sums[i] = a.output[i] + b.output[i];
      products[i] = a.output[i] * b.output[i];
    }
    AUD_CHECK(sameBits(sum, sums.data(), kFrames));
    AUD_CHECK(sameBits(product, products.data(), kFrames));
    AUD_CHECK(sameBits(single, a.output.data(), kFrames));
    endBlock(matrix);
  }
}

AUD_TEST(sfizz_mod_matrix_hands_a_further_source_the_first_one_as_before) {
  sfz::ModMatrix matrix;
  matrix.setSamplesPerBlock(kFrames);
  std::vector<const TestSource*> order;
  TestSource a(matrix, 3, order);
  TestSource b(matrix, 4, order);
  const auto sourceA = matrix.registerSource(lfoKey(0), a);
  const auto sourceB = matrix.registerSource(lfoKey(1), b);
  const auto pitch = matrix.registerTarget(targetKey(sfz::ModId::Pitch));
  AUD_CHECK(connect(matrix, sourceA, pitch));
  AUD_CHECK(connect(matrix, sourceB, pitch));
  // Both read the target while they generate.
  a.readWhileGenerating(pitch);
  b.readWhileGenerating(pitch);
  startVoice(matrix);
  // The buffer of a target starts with zeros.
  std::vector<float> before(kFrames, 0.0f);
  for (int block = 0; block < kBlocks; ++block) {
    order.clear();
    beginBlock(matrix);
    const float* sum = matrix.getModulation(pitch);
    AUD_CHECK(sum != nullptr && order.size() == 2);
    if (sum == nullptr || order.size() != 2) return;
    const TestSource& first = *order[0];
    const TestSource& further = *order[1];
    // The further source finds the first in the target's buffer.
    AUD_CHECK(sameBits(further.seen.data(), first.output.data(), kFrames));
    // The first finds the output of the block before there.
    AUD_CHECK(sameBits(first.seen.data(), before.data(), kFrames));
    std::vector<float> sums(kFrames);
    for (unsigned i = 0; i < kFrames; ++i) sums[i] = a.output[i] + b.output[i];
    AUD_CHECK(sameBits(sum, sums.data(), kFrames));
    before.assign(sum, sum + kFrames);
    endBlock(matrix);
  }
}

AUD_TEST(sfizz_mod_matrix_hands_a_cycle_the_output_of_the_block_before) {
  sfz::ModMatrix matrix;
  matrix.setSamplesPerBlock(kFrames);
  std::vector<const TestSource*> order;
  TestSource a(matrix, 5, order);
  const auto sourceA = matrix.registerSource(lfoKey(0), a);
  // A single source at depth 1, which reads its own target.
  const auto pitch = matrix.registerTarget(targetKey(sfz::ModId::Pitch));
  AUD_CHECK(connect(matrix, sourceA, pitch));
  a.readWhileGenerating(pitch);
  startVoice(matrix);
  // The buffer of a target starts with zeros.
  std::vector<float> before(kFrames, 0.0f);
  for (int block = 0; block < kBlocks; ++block) {
    beginBlock(matrix);
    const float* output = matrix.getModulation(pitch);
    AUD_CHECK(output != nullptr && a.read != nullptr);
    if (output == nullptr || a.read == nullptr) return;
    // The target hands out its source, and the buffer the source read from
    // holds it too: a reader that keeps that buffer finds the output.
    AUD_CHECK(sameBits(output, a.output.data(), kFrames));
    AUD_CHECK(sameBits(a.read, a.output.data(), kFrames));
    // The source found the output of the block before.
    AUD_CHECK(sameBits(a.seen.data(), before.data(), kFrames));
    before.assign(output, output + kFrames);
    endBlock(matrix);
  }
}

// ############################################################################
// Instruments

namespace {

constexpr int kBlock = 128;
constexpr int kRenderBlocks = 120;

// Targets with a single source at depth 1 for every kind of reader - the
// voice, an LFO, a filter and an EQ. On a sine without a filter: the volume
// on LFO 1, the frequency of LFO 1 on a flex EG, which LFO 1 reads while it
// generates, the pitch on LFO 2, and the pan on CC 10, as every region has
// it. On a saw with a filter and an EQ: the cutoff on LFO 3 and the gain of
// the EQ on LFO 4.
const char* const kSine =
    "<region> sample=*sine lokey=48 hikey=59 pitch_keycenter=54 "
    "lfo1_freq=3.5 lfo1_volume=1 eg1_time1=0.2 eg1_level1=1 eg1_time2=0.3 eg1_level2=0.25 eg1_sustain=2 "
    "eg1_freq_lfo1=1 lfo2_freq=6 lfo2_pitch=1";
const char* const kSaw =
    "<region> sample=*saw lokey=60 hikey=71 pitch_keycenter=66 "
    "fil1_type=lpf_2p cutoff1=1500 lfo3_freq=2 lfo3_cutoff1=1 "
    "eq1_freq=1200 eq1_bw=1 eq1_gain=6 lfo4_freq=4.5 lfo4_eq1gain=1";

// A further source on each of those targets that changes no bit: a CC at
// depth 1 that holds 0. The target then copies its first source and adds
// the other, which gives the source that is not 0, bit for bit.
const char* const kSineNeutral = " volume_oncc21=1 pitch_oncc22=1 lfo1_freq_oncc23=1 pan_oncc24=100";
const char* const kSawNeutral = " cutoff1_oncc25=1 eq1_gain_oncc26=1 pan_oncc24=100";

std::string instrument(bool neutral) {
  return std::string("<control> set_hdcc21=0 set_hdcc22=0 set_hdcc23=0 set_hdcc24=0 set_hdcc25=0 "
                     "set_hdcc26=0\n") +
         kSine + (neutral ? kSineNeutral : "") + "\n" + kSaw + (neutral ? kSawNeutral : "") + "\n";
}

// The output of `text`, both channels one after the other per block: notes
// on both regions, the pan on CC 10 moved, the release. Empty if it does
// not load.
std::vector<float> render(const std::string& text) {
  sfz::Synth synth;
  synth.setSampleRate(48000);
  synth.setSamplesPerBlock(kBlock);
  if (!synth.loadSfzString("/aud_dsp_sampler/mod_matrix.sfz", text)) return {};
  std::vector<float> left(kBlock);
  std::vector<float> right(kBlock);
  float* channels[2] = {left.data(), right.data()};
  std::vector<float> output;
  for (int block = 0; block < kRenderBlocks; ++block) {
    if (block == 0) {
      synth.noteOn(0, 50, 100);
      synth.noteOn(17, 64, 90);
    }
    if (block == 30) synth.hdcc(5, 10, 0.9f);
    if (block == 40) synth.noteOn(33, 57, 127);
    if (block == 50) synth.hdcc(70, 10, 0.2f);
    if (block == 80) {
      synth.noteOff(0, 50, 0);
      synth.noteOff(9, 64, 0);
      synth.noteOff(100, 57, 0);
    }
    synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, kBlock));
    output.insert(output.end(), left.begin(), left.end());
    output.insert(output.end(), right.begin(), right.end());
  }
  return output;
}

float peakOf(const std::vector<float>& samples) {
  float peak = 0.0f;
  for (float sample : samples) peak = std::max(peak, std::abs(sample));
  return peak;
}

}  // namespace

AUD_TEST(sfizz_mod_matrix_renders_a_single_source_as_its_copy) {
  const std::vector<float> shortcut = render(instrument(false));
  const std::vector<float> copied = render(instrument(true));
  AUD_CHECK(!shortcut.empty() && shortcut.size() == copied.size());
  if (shortcut.empty() || shortcut.size() != copied.size()) return;
  AUD_CHECK(peakOf(shortcut) > 0.05f);
  AUD_CHECK(sameBits(shortcut.data(), copied.data(), shortcut.size()));
  // The modulations change the render, so that the comparison shows them.
  const std::string plain =
      "<region> sample=*sine lokey=48 hikey=59 pitch_keycenter=54\n"
      "<region> sample=*saw lokey=60 hikey=71 pitch_keycenter=66 fil1_type=lpf_2p cutoff1=1500 eq1_freq=1200 "
      "eq1_bw=1 eq1_gain=6\n";
  const std::vector<float> unmodulated = render(plain);
  AUD_CHECK(unmodulated.size() == shortcut.size() && !sameBits(unmodulated.data(), shortcut.data(), shortcut.size()));
}

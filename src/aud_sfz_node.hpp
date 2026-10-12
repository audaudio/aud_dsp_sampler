// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The SFZ sampler node (ticket 25): the vtable of aud.sampler.sfz over the
// package's fork of sfizz. The node and its load tasks share a state that a
// task keeps alive after the node is gone. Written in C++14: the hook
// compiles the node with sfizz's C sources, so no -std flag can be passed.

#ifndef AUD_SFZ_NODE_HPP
#define AUD_SFZ_NODE_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "aud_abi.h"

#include "aud_dsp_sampler.h"

namespace sfz {
class Synth;
}

namespace aud_sfz {

// ############################################################################
// Constants

// The parameters of the node by index (AUD_SFZ_PARAM_*): their ranges and
// defaults hold here and nowhere else.
constexpr AudParamDescriptor kParams[AUD_SFZ_NUM_PARAMS] = {
    {sizeof(AudParamDescriptor), "volume", "Volume", "dB", -60.0f, 6.0f, 0.0f,
     AUD_PARAM_AUTOMATABLE | AUD_PARAM_RAMPED, 0, 0},
    {sizeof(AudParamDescriptor), "polyphony", "Polyphony", "", 1.0f, 256.0f, 64.0f,
     AUD_PARAM_STEPPED, 255, 0},
    {sizeof(AudParamDescriptor), "sample_quality", "Sample quality", "", 0.0f, 10.0f, 2.0f,
     AUD_PARAM_AUTOMATABLE | AUD_PARAM_STEPPED, 10, 0},
    {sizeof(AudParamDescriptor), "oscillator_quality", "Oscillator quality", "", 0.0f, 3.0f,
     1.0f, AUD_PARAM_AUTOMATABLE | AUD_PARAM_STEPPED, 3, 0},
    // The polyphony and the tuning load the instrument again: notes that
    // sound fade out, so they are no automation targets.
    {sizeof(AudParamDescriptor), "tuning_frequency", "Tuning frequency", "Hz", 400.0f, 480.0f,
     440.0f, 0, 0, 0},
    {sizeof(AudParamDescriptor), "stretch_tuning", "Stretch tuning", "", 0.0f, 1.0f, 0.0f, 0, 0,
     0},
};

// The sample rate, the block and the channels a node assumes until its
// first prepare.
constexpr double kDefaultSampleRate = 48000;
constexpr uint32_t kDefaultMaxFrames = 1024;
constexpr uint32_t kDefaultChannels = 2;
// The crossfade from an instrument to the next one.
constexpr double kCrossfadeSeconds = 0.005;
constexpr uint32_t kDefaultFadeFrames =
    static_cast<uint32_t>(kCrossfadeSeconds * kDefaultSampleRate + 0.5);
// The ramp of an immediate volume change, so that it does not click.
constexpr uint32_t kVolumeRampFrames = 64;
// The controllers the node remembers for a new synth: sfizz's extended CCs.
constexpr uint32_t kNumCcs = 512;
// Synths the audio thread hands back to a worker and could not post yet.
constexpr uint32_t kRetiredSlots = 4;

// The step of a stepped parameter, rounded: automation may land next to a
// step, e.g. on 2.9999998 for 3.
constexpr int stepOf(float value) {
  return static_cast<int>(value < 0 ? value - 0.5f : value + 0.5f);
}

// The default of a parameter, and of a stepped one as its step.
constexpr float defaultOf(uint32_t index) { return kParams[index].default_value; }
constexpr int defaultStepOf(uint32_t index) { return stepOf(defaultOf(index)); }

// ############################################################################
// The state of a node

// What the node plays: an SFZ file or an SFZ text with the virtual path its
// samples resolve against, and an optional Scala tuning.
struct Source {
  enum class Kind { none, file, text };
  Kind kind = Kind::none;
  std::string file;
  std::string text;
  std::string virtualPath = "/aud_dsp_sampler/instrument.sfz";
  std::string scalaFile;
  std::string scalaText;
};

// How a synth is configured: the format of the graph and the parameters a
// synth takes only while it does not render or that allocate: the voices
// and the tuning.
struct Config {
  double sampleRate = kDefaultSampleRate;
  uint32_t maxFrames = kDefaultMaxFrames;
  uint32_t channels = kDefaultChannels;
  // Raised by every prepare: a load configured before applies it again.
  uint32_t generation = 0;
  // Copied from the atomics of Shared when a load starts.
  int polyphony = defaultStepOf(AUD_SFZ_PARAM_POLYPHONY);
  int sampleQuality = defaultStepOf(AUD_SFZ_PARAM_SAMPLE_QUALITY);
  int oscillatorQuality = defaultStepOf(AUD_SFZ_PARAM_OSCILLATOR_QUALITY);
  float tuningFrequency = defaultOf(AUD_SFZ_PARAM_TUNING_FREQUENCY);
  float stretchTuning = defaultOf(AUD_SFZ_PARAM_STRETCH_TUNING);
};

struct Shared;

// A reload after a change of the polyphony or the tuning, posted by the
// audio thread without allocating: it keeps the shared state alive while
// it waits.
struct Reload {
  std::shared_ptr<Shared> keep;
  std::atomic<bool> posted{false};
};

// What the node shares with its load tasks.
struct Shared {
  const AudHostApi* host = nullptr;
  // Guards config, built, the sources, destroyed, the tail, the loads and
  // the deferred reload; never taken on the audio thread.
  std::mutex mutex;
  Config config;
  // The configuration and the source of the newest synth a load handed
  // over; a reload builds them again.
  Config built;
  Source builtSource;
  Source source;
  bool destroyed = false;
  float tailSeconds = 0;  // the longest release of the newest instrument
  // The loads that run; a reload stands back while one does, and the last
  // load to end runs the reload it deferred.
  int loads = 0;
  bool reloadDeferred = false;
  // The parameters the audio thread sets and a load reads.
  std::atomic<int> polyphony{defaultStepOf(AUD_SFZ_PARAM_POLYPHONY)};
  std::atomic<int> sampleQuality{defaultStepOf(AUD_SFZ_PARAM_SAMPLE_QUALITY)};
  std::atomic<int> oscillatorQuality{defaultStepOf(AUD_SFZ_PARAM_OSCILLATOR_QUALITY)};
  std::atomic<float> tuningFrequency{defaultOf(AUD_SFZ_PARAM_TUNING_FREQUENCY)};
  std::atomic<float> stretchTuning{defaultOf(AUD_SFZ_PARAM_STRETCH_TUNING)};
  // The newest load, taken under the mutex; an older one cancels itself.
  std::atomic<uint64_t> serial{0};
  // A loaded synth the audio thread adopts at its next block.
  std::atomic<sfz::Synth*> ready{nullptr};
  Reload reload;
};

// One instance of aud.sampler.sfz.
struct Node {
  // A node with its parameters at their defaults.
  Node();

  std::shared_ptr<Shared> shared;
  const AudHostApi* host = nullptr;

  // Realtime thread.
  sfz::Synth* active = nullptr;
  sfz::Synth* fading = nullptr;
  uint32_t fadeFrames = kDefaultFadeFrames;
  uint32_t fadeRemaining = 0;
  sfz::Synth* retired[kRetiredSlots] = {};
  std::vector<float> scratch;  // the fading synth's block, channel-major
  std::vector<float*> scratchPointers;
  std::vector<float*> outputPointers;
  uint32_t channels = kDefaultChannels;
  float params[AUD_SFZ_NUM_PARAMS] = {};
  float gain = 1;
  float gainTarget = 1;
  float gainStep = 0;
  uint32_t gainRemaining = 0;
  bool freewheeling = false;
  // Controllers a new synth takes over.
  float cc[kNumCcs] = {};
  bool ccSet[kNumCcs] = {};
  float pitchBend = 0;
  bool pitchBendSet = false;
  float aftertouch = 0;
  bool aftertouchSet = false;
  int program = -1;
  // The transport as the synth last heard it.
  double tempo = -1;
  uint32_t numerator = 0;
  uint32_t denominator = 0;
  int playing = -1;
  // Per block.
  uint32_t unsupported = 0;
  void* client = nullptr;  // the sfz::Client of the messages
};

// ############################################################################
// The vtable entries

void* create(const AudNodeDescriptor* descriptor, const AudHostApi* host);
void destroy(void* instance);
int32_t prepare(void* instance, const AudPrepareInfo* info);
void reset(void* instance, uint32_t reason);
void setParam(void* instance, uint32_t index, float value);
void process(void* instance, const AudProcessContext* context);
int32_t setString(void* instance, uint32_t key, const char* value);
uint32_t getTail(void* instance);

// The store of the process as a cache of the engine.
uint64_t trimStore(void* cache, uint64_t bytes);
extern const AudCacheVTable kStoreVTable;

}  // namespace aud_sfz

#endif  // AUD_SFZ_NODE_HPP

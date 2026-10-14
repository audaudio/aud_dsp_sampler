// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_sfz_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>

#include "aud_ump.h"

#include "FilePool.h"
#include "Messaging.h"
#include "Region.h"
#include "Synth.h"

#include "aud_apk_assets.hpp"

namespace aud_sfz {
namespace {

// The longest finite tail, in frames.
constexpr uint32_t kLongestTail = AUD_TAIL_INFINITE - 1;
// The arguments of a message to sfizz: no path of sfizz takes more.
constexpr uint32_t kMaxMessageArgs = 4;
// The arguments of an answer of sfizz the node passes on.
constexpr uint32_t kMaxReplyArgs = 16;
// The bytes of a MIDI message in an answer of sfizz.
constexpr uint32_t kMidiMessageBytes = 4;
// The largest value of a 7-bit MIDI 1.0 data byte, and the centre of the
// 14-bit pitch bend.
constexpr float kMax7Bit = 127.0f;
constexpr int kPitchBendCentre = 8192;
// The mask of a 7-bit MIDI 1.0 data byte, and the shift of the program in
// the second word of a MIDI 2.0 program change.
constexpr uint32_t kDataMask = 0x7f;
constexpr uint32_t kMidi2ProgramShift = 24;

// ............................................................................
// The engine as the host of the pools (ticket 25)

// [any thread] Queues a task for a worker of the engine; AUD_OK or the error
// code of post_task.
int32_t post(const AudHostApi* host, void (*run)(void*), void* user) {
  const AudTask task{sizeof(AudTask), 0, run, user};
  return host->post_task(host->host, &task);
}

bool postTask(void* context, void (*run)(void*), void* user) {
  return post(static_cast<const AudHostApi*>(context), run, user) == AUD_OK;
}

void bytesChanged(void* context, int64_t delta) {
  const auto* host = static_cast<const AudHostApi*>(context);
  host->cache_changed(host->host, &sfz::SampleStore::get(), delta);
}

sfz::FileHost fileHostOf(const AudHostApi* host) {
  sfz::FileHost fileHost;
  fileHost.context = const_cast<AudHostApi*>(host);
  fileHost.post = postTask;
  fileHost.bytesChanged = bytesChanged;
  return fileHost;
}

void deleteSynth(void* user) { delete static_cast<sfz::Synth*>(user); }

float gainOf(float db) { return std::pow(10.0f, db / 20.0f); }

// [control] Logs an error of the node, when the host takes logs.
void logError(const AudHostApi* host, const char* message) {
  if (host->log != nullptr) host->log(host->host, AUD_LOG_ERROR, message);
}

// ............................................................................
// Synths that do not render

// Applies what a synth takes only while it does not render or what
// allocates, and the realtime settings with it.
void configure(sfz::Synth& synth, const Config& config) {
  synth.setSampleRate(static_cast<float>(config.sampleRate));
  synth.setSamplesPerBlock(static_cast<int>(config.maxFrames));
  synth.setNumVoices(config.polyphony);
  synth.setSampleQuality(sfz::Synth::ProcessLive, config.sampleQuality);
  synth.setOscillatorQuality(sfz::Synth::ProcessLive, config.oscillatorQuality);
  synth.setTuningFrequency(config.tuningFrequency);
  synth.loadStretchTuningByRatio(config.stretchTuning);
}

// The configuration with the parameters the audio thread set last.
Config snapshot(Shared& shared) {
  Config config = shared.config;
  config.polyphony = shared.polyphony.load(std::memory_order_acquire);
  config.sampleQuality = shared.sampleQuality.load(std::memory_order_acquire);
  config.oscillatorQuality = shared.oscillatorQuality.load(std::memory_order_acquire);
  config.tuningFrequency = shared.tuningFrequency.load(std::memory_order_acquire);
  config.stretchTuning = shared.stretchTuning.load(std::memory_order_acquire);
  return config;
}

bool sameSynthConfig(const Config& a, const Config& b) {
  return a.generation == b.generation && a.polyphony == b.polyphony &&
         a.sampleQuality == b.sampleQuality && a.oscillatorQuality == b.oscillatorQuality &&
         a.tuningFrequency == b.tuningFrequency && a.stretchTuning == b.stretchTuning;
}

// The release of a flex EG: the stages after its sustain point, each at
// the longest time its controllers give it (ticket 27).
float flexReleaseOf(const sfz::FlexEGDescription& eg) {
  float seconds = 0;
  for (size_t i = static_cast<size_t>(std::max(0, eg.sustain)) + 1; i < eg.points.size(); ++i) {
    const sfz::FlexEGPoint& point = eg.points[i];
    seconds += std::max(0.0f, point.time);
    for (const auto& modulation : point.ccTime) seconds += std::max(0.0f, modulation.data);
  }
  return seconds;
}

// The longest amplitude release of an instrument at full velocity, in
// seconds: of the ADSR, or of the flex EG that replaces it (egN_ampeg).
float releaseOf(const sfz::Synth& synth) {
  float longest = 0;
  for (int i = 0, count = synth.getNumRegions(); i < count; ++i) {
    const sfz::Region* region = synth.getRegionView(i);
    if (region == nullptr) continue;
    if (region->flexAmpEG && *region->flexAmpEG < region->flexEGs.size()) {
      longest = std::max(longest, flexReleaseOf(region->flexEGs[*region->flexAmpEG]));
      continue;
    }
    const sfz::EGDescription& eg = region->amplitudeEG;
    longest = std::max(longest, eg.release + std::max(0.0f, eg.vel2release));
  }
  return longest;
}

// "<prefix> 2 <things>: a, b"; empty without names.
std::string listOf(const std::string& prefix, const char* one, const char* many,
                   const std::vector<std::string>& names) {
  if (names.empty()) return std::string();
  std::string text = prefix + " " + std::to_string(names.size()) + " " +
                     (names.size() == 1 ? one : many) + ": ";
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) text += ", ";
    text += names[i];
  }
  return text;
}

// A size for a message, e.g. "32 KB" or "1.5 MB".
std::string sizeText(uint64_t bytes) {
  constexpr uint64_t kKilobyte = 1024;
  constexpr uint64_t kMegabyte = kKilobyte * kKilobyte;
  char text[32];
  if (bytes < kMegabyte) {
    std::snprintf(text, sizeof(text), "%llu KB",
                  static_cast<unsigned long long>((bytes + kKilobyte - 1) / kKilobyte));
  } else {
    std::snprintf(text, sizeof(text), "%.1f MB",
                  static_cast<double>(bytes) / static_cast<double>(kMegabyte));
  }
  return text;
}

// What a load could not read, e.g. "sfizz could not read 2 samples: a.wav,
// b.wav"; empty when it read everything.
std::string missingOf(const sfz::Synth& synth) {
  return listOf("sfizz could not read", "sample", "samples", synth.getMissingSamples());
}

// What a load that ended well could not do: the samples it could not read
// and the effects it does not know, which pass the signal through.
std::string notesOf(const sfz::Synth& synth) {
  std::string text = missingOf(synth);
  const std::string effects = listOf("sfizz does not know", "effect type", "effect types",
                                     synth.getUnsupportedEffects());
  if (!effects.empty()) text += (text.empty() ? "" : "; ") + effects;
  return text;
}

// Loads what `source` names into `synth`, the Scala tuning last; false with
// `what` naming the part that failed.
bool loadSource(sfz::Synth& synth, const Source& source, std::string& what) {
  bool loaded = true;
  switch (source.kind) {
    case Source::Kind::file:
      what = source.file;
      loaded = synth.loadSfzFile(source.file);
      break;
    case Source::Kind::text:
      what = source.virtualPath;
      loaded = synth.loadSfzString(source.virtualPath, source.text);
      break;
    case Source::Kind::none:
      break;
  }
  if (loaded && !source.scalaFile.empty()) {
    what = source.scalaFile;
    loaded = synth.loadScalaFile(source.scalaFile);
  } else if (loaded && !source.scalaText.empty()) {
    what = "the Scala text";
    loaded = synth.loadScalaString(source.scalaText);
  }
  return loaded;
}

// Why the paths of `source` cannot be read on this platform, e.g. an apk:/
// path outside Android (ticket 27); empty when they can.
std::string unavailableOf(const Source& source) {
  std::string reason;
  if (source.kind == Source::Kind::file) reason = unavailableReason(source.file);
  if (source.kind == Source::Kind::text) reason = unavailableReason(source.virtualPath);
  if (reason.empty()) reason = unavailableReason(source.scalaFile);
  return reason;
}

// Why a load failed: what sfizz could not load and the first error of its
// parser.
std::string failureOf(const sfz::Synth& synth, const std::string& what) {
  std::string message = "sfizz could not load " + what;
  const std::vector<std::string>& errors = synth.getParseErrors();
  if (!errors.empty()) message += ": " + errors.front();
  return message;
}

// ............................................................................
// Loads on a worker thread

struct LoadJob {
  std::shared_ptr<Shared> shared;
  int32_t load = 0;  // 0: a reload of the engine's own, without an id
  uint64_t serial = 0;
};

void endLoad(const LoadJob& job, int32_t result, const char* message) {
  if (job.load <= 0) return;
  const AudHostApi* host = job.shared->host;
  host->end_load(host->host, job.load, result, message);
}

// Whether a newer load, the node's end or the control thread stopped the
// load.
bool stopped(const LoadJob& job) {
  Shared& shared = *job.shared;
  if (shared.serial.load(std::memory_order_acquire) != job.serial) return true;
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (shared.destroyed) return true;
  }
  return job.load > 0 && shared.host->load_cancelled(shared.host->host, job.load);
}

// Hands a loaded synth to the audio thread unless a newer load or the
// node's end came first; false then, and the synth goes. A prepare or a
// parameter since the configuration reaches the synth here, as it does not
// render yet. Nothing changes before the last step that may throw.
bool handOver(const LoadJob& job, std::unique_ptr<sfz::Synth> synth, Source source,
              float release, const Config& config) {
  Shared& shared = *job.shared;
  sfz::Synth* previous = nullptr;
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (shared.destroyed || shared.serial.load(std::memory_order_acquire) != job.serial) {
      return false;
    }
    const Config now = snapshot(shared);
    if (!sameSynthConfig(now, config)) configure(*synth, now);
    shared.built = now;
    shared.builtSource = std::move(source);
    shared.tailSeconds = release;
    previous = shared.ready.exchange(synth.release(), std::memory_order_acq_rel);
  }
  delete previous;  // a synth no block adopted
  return true;
}

// Fills a standby synth with the instrument the node names now and hands it
// to the audio thread; the node plays the old one meanwhile (decision 3 of
// the plan review of ticket 25).
void runLoad(const LoadJob& job) {
  Shared& shared = *job.shared;
  const AudHostApi* host = shared.host;
  Config config;
  Source source;
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    config = snapshot(shared);
    // A reload builds again what plays, not what a failed load named.
    source = job.load > 0 ? shared.source : shared.builtSource;
  }
  const std::string unavailable = unavailableOf(source);
  if (!unavailable.empty()) {
    endLoad(job, AUD_ERROR_UNSUPPORTED, unavailable.c_str());
    return;
  }
  std::unique_ptr<sfz::Synth> synth(new sfz::Synth());
  configure(*synth, config);
  sfz::FilePool& pool = synth->getResources().getFilePool();
  pool.setHost(fileHostOf(host));
  pool.setLoadObserver([&job, host](float progress) {
    if (stopped(job)) return false;
    if (job.load > 0) host->load_progress(host->host, job.load, progress);
    return true;
  });
  std::string what;
  const bool loaded = loadSource(*synth, source, what);
  const bool cancelled = pool.loadCancelled() || stopped(job);
  pool.setLoadObserver(nullptr);
  if (cancelled) {
    endLoad(job, AUD_ERROR_CANCELLED, nullptr);
    return;
  }
  if (!loaded) {
    endLoad(job, AUD_ERROR_FAILED, failureOf(*synth, what).c_str());
    return;
  }
  // sfizz drops the regions whose samples it cannot read: without a region
  // left the load fails, otherwise it ends well and names them.
  const std::string missing = missingOf(*synth);
  if (!missing.empty() && synth->getNumRegions() == 0) {
    endLoad(job, AUD_ERROR_FAILED, missing.c_str());
    return;
  }
  // The heads an instrument pins stay in the store while it plays; one whose
  // heads alone exceed the sample cache budget is refused.
  const uint64_t pinned = pool.pinnedBytes();
  const uint64_t budget = host->cache_budget(host->host);
  if (pinned > budget) {
    const std::string message = "the samples of the instrument pin " + sizeText(pinned) +
                                ", more than the sample cache budget of " + sizeText(budget);
    endLoad(job, AUD_ERROR_CAPACITY, message.c_str());
    return;
  }
  const float release = releaseOf(*synth);
  const std::string notes = notesOf(*synth);
  if (!handOver(job, std::move(synth), std::move(source), release, config)) {
    endLoad(job, AUD_ERROR_CANCELLED, nullptr);
    return;
  }
  endLoad(job, AUD_OK, notes.empty() ? nullptr : notes.c_str());
}

// Runs a load. An exception, which means memory ran out, ends the load
// instead of the process: it would cross the C ABI of the worker.
void runLoadSafely(const LoadJob& job) noexcept {
  try {
    runLoad(job);
  } catch (const std::bad_alloc&) {
    endLoad(job, AUD_ERROR_OUT_OF_MEMORY, nullptr);
  } catch (const std::exception& error) {
    endLoad(job, AUD_ERROR_FAILED, error.what());
  }
}

// [control, worker] Counts a load that starts and gives it the newest
// serial; under the mutex, so that a reload and a load that starts at the
// same time agree on which one is newer.
uint64_t beginLoad(Shared& shared) {
  shared.loads += 1;
  return shared.serial.fetch_add(1, std::memory_order_acq_rel) + 1;
}

// [control, worker] Counts a load that ended; true when it was the last one
// and a reload waits for it.
bool endLoads(Shared& shared) {
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.loads -= 1;
  if (shared.loads > 0 || !shared.reloadDeferred) return false;
  shared.reloadDeferred = false;
  return true;
}

// Whether a reload runs now, counted as a load with the newest serial. It
// stands back while a load runs, which takes the change when it hands its
// synth over, and the last load to end runs it; it is not needed when what
// was built has the configuration already.
bool beginReload(LoadJob& job) {
  Shared& shared = *job.shared;
  std::lock_guard<std::mutex> lock(shared.mutex);
  if (shared.loads > 0) {
    shared.reloadDeferred = true;
    return false;
  }
  shared.reloadDeferred = false;
  if (shared.destroyed || shared.builtSource.kind == Source::Kind::none ||
      sameSynthConfig(snapshot(shared), shared.built)) {
    return false;
  }
  job.serial = beginLoad(shared);
  return true;
}

// A change of the polyphony or the tuning loads the instrument again: sfizz
// sets its voices only while it does not render, and its tuning tables
// allocate. Runs reloads until none waits.
void reloadNow(const std::shared_ptr<Shared>& shared) {
  LoadJob job;
  job.shared = shared;
  while (beginReload(job)) {
    runLoadSafely(job);
    if (!endLoads(*shared)) return;
  }
}

void reloadTask(void* user) {
  Reload& reload = *static_cast<Reload*>(user);
  std::shared_ptr<Shared> shared = std::move(reload.keep);
  reload.posted.store(false, std::memory_order_release);
  if (shared) reloadNow(shared);
}

// [any thread] Posts the reload unless it waits already. On the audio
// thread too: the copy of the pointer is an atomic increment, and the node
// holds a reference, so a reset frees nothing.
void postReload(const std::shared_ptr<Shared>& shared, const AudHostApi* host) {
  Reload& reload = shared->reload;
  if (reload.posted.exchange(true, std::memory_order_acq_rel)) return;
  reload.keep = shared;
  if (post(host, reloadTask, &reload) != AUD_OK) {
    reload.keep.reset();
    reload.posted.store(false, std::memory_order_release);
  }
}

void loadTask(void* user) {
  std::unique_ptr<LoadJob> job(static_cast<LoadJob*>(user));
  const std::shared_ptr<Shared> shared = job->shared;
  runLoadSafely(*job);
  job.reset();
  if (endLoads(*shared)) reloadNow(shared);
}

// [control] Starts a load of what the node names now; returns its id.
int32_t startLoad(Node& node, const std::string& name) {
  const AudHostApi* host = node.host;
  const int32_t id = host->begin_load(host->host, &node, name.c_str());
  if (id <= 0) return id;
  auto* job = new (std::nothrow) LoadJob();
  if (job == nullptr) {
    host->end_load(host->host, id, AUD_ERROR_OUT_OF_MEMORY, nullptr);
    return AUD_ERROR_OUT_OF_MEMORY;
  }
  job->shared = node.shared;
  job->load = id;
  {
    std::lock_guard<std::mutex> lock(node.shared->mutex);
    job->serial = beginLoad(*node.shared);
  }
  const int32_t posted = post(host, loadTask, job);
  if (posted != AUD_OK) {
    if (endLoads(*node.shared)) postReload(node.shared, host);
    host->end_load(host->host, id, posted, "no worker thread took the load");
    delete job;
    return posted;
  }
  return id;
}

// ............................................................................
// String settings on the control thread

// What a string setting does: start a load with a name, or end with a
// result code.
struct Setting {
  bool loads = false;
  int32_t result = AUD_OK;
  std::string name;
};

// Changes the source as a string setting says. Builds the new source aside
// and commits it last, so that memory that runs out changes nothing.
Setting changeSource(Shared& shared, uint32_t key, const char* value) {
  const std::string text = value != nullptr ? value : "";
  Setting setting;
  std::lock_guard<std::mutex> lock(shared.mutex);
  Source source = shared.source;
  switch (key) {
    case AUD_SFZ_KEY_FILE:
      source.kind = text.empty() ? Source::Kind::none : Source::Kind::file;
      source.file = text;
      setting.loads = true;
      setting.name = text;
      break;
    case AUD_SFZ_KEY_VIRTUAL_PATH:
      // The path the next text resolves its samples against: a text and
      // its path come together, so the path starts no load of its own.
      source.virtualPath = text.empty() ? Source().virtualPath : text;
      break;
    case AUD_SFZ_KEY_TEXT:
      source.kind = text.empty() ? Source::Kind::none : Source::Kind::text;
      source.text = text;
      setting.loads = true;
      setting.name = source.virtualPath;
      break;
    case AUD_SFZ_KEY_SCALA_FILE:
      source.scalaFile = text;
      source.scalaText.clear();
      setting.loads = source.kind != Source::Kind::none;
      setting.name = text;
      break;
    case AUD_SFZ_KEY_SCALA_TEXT:
      source.scalaText = text;
      source.scalaFile.clear();
      setting.loads = source.kind != Source::Kind::none;
      setting.name = "scala";
      break;
    default:
      setting.result = AUD_ERROR_INVALID_ARGUMENT;
      return setting;
  }
  shared.source = std::move(source);
  return setting;
}

// ............................................................................
// The audio thread

// Hands the synths the audio thread is done with to a worker; what the
// queue refuses waits for the next block.
void postRetired(Node& node) {
  for (sfz::Synth*& synth : node.retired) {
    if (synth != nullptr && post(node.host, deleteSynth, synth) == AUD_OK) synth = nullptr;
  }
}

bool hasFreeSlot(const Node& node) {
  for (const sfz::Synth* slot : node.retired) {
    if (slot == nullptr) return true;
  }
  return false;
}

// A synth goes to a worker: process adopts a synth only while no fade runs
// and a slot is free, and only the end of that synth's fade fills one, so a
// slot is always free here.
void retire(Node& node, sfz::Synth* synth) {
  for (sfz::Synth*& slot : node.retired) {
    if (slot == nullptr) {
      slot = synth;
      return;
    }
  }
}

// The settings a synth takes while it renders, without allocating.
void applyRealtimeParams(const Node& node, sfz::Synth& synth) {
  synth.setSampleQuality(sfz::Synth::ProcessLive,
                         stepOf(node.params[AUD_SFZ_PARAM_SAMPLE_QUALITY]));
  synth.setOscillatorQuality(sfz::Synth::ProcessLive,
                             stepOf(node.params[AUD_SFZ_PARAM_OSCILLATOR_QUALITY]));
}

// Adopts a synth a load handed over: the one that played fades out, the
// new one takes over the controllers, the realtime parameters and the
// transport; the load configured the rest.
void adopt(Node& node, sfz::Synth* next) {
  node.fading = node.active;
  node.fadeRemaining = node.fading != nullptr ? node.fadeFrames : 0;
  node.active = next;
  if (node.freewheeling) next->enableFreeWheeling();
  // Restored, not sent: a region a controller triggers does not start.
  for (uint32_t cc = 0; cc < kNumCcs; ++cc) {
    if (node.ccSet[cc]) next->restoreHdcc(static_cast<int>(cc), node.cc[cc]);
  }
  if (node.pitchBendSet) next->hdPitchWheel(0, node.pitchBend);
  if (node.aftertouchSet) next->hdChannelAftertouch(0, node.aftertouch);
  if (node.program >= 0) next->programChange(0, node.program);
  // The controllers are where the old synth left them, not on their way.
  next->resetControllerSmoothers();
  applyRealtimeParams(node, *next);
  node.tempo = -1;
  node.numerator = 0;
  node.denominator = 0;
  node.playing = -1;
}

// A synth a load handed over, adopted at the block start unless a fade
// runs, which takes at most 5 ms.
void adoptReady(Node& node) {
  if (node.fading != nullptr || !hasFreeSlot(node)) return;
  sfz::Synth* next = node.shared->ready.exchange(nullptr, std::memory_order_acq_rel);
  if (next != nullptr) adopt(node, next);
}

// The synths render with sfizz's best quality while the engine renders
// offline.
void followFreewheeling(Node& node, const AudProcessContext* context) {
  const bool offline = (context->flags & AUD_PROCESS_OFFLINE) != 0;
  if (offline == node.freewheeling) return;
  node.freewheeling = offline;
  sfz::Synth* synths[2] = {node.active, node.fading};
  for (sfz::Synth* synth : synths) {
    if (synth == nullptr) continue;
    if (offline) {
      synth->enableFreeWheeling();
    } else {
      synth->disableFreeWheeling();
    }
  }
}

void setGainTarget(Node& node, float db, uint32_t rampFrames) {
  node.gainTarget = gainOf(db);
  node.gainRemaining = std::max<uint32_t>(1, rampFrames);
  node.gainStep = (node.gainTarget - node.gain) / static_cast<float>(node.gainRemaining);
}

// A parameter within its range; NaN counts as its minimum.
float clampParam(uint32_t index, float value) {
  return std::min(kParams[index].max_value, std::max(kParams[index].min_value, value));
}

// Stores a setting a synth takes only when it is built; a change loads the
// instrument again on a worker.
template <typename T>
void reloadIfChanged(Node& node, std::atomic<T>& setting, T value) {
  if (setting.exchange(value, std::memory_order_acq_rel) != value) {
    postReload(node.shared, node.host);
  }
}

// A parameter, from set_param or from an event of the block.
void applyParam(Node& node, uint32_t index, float value, uint32_t rampFrames) {
  if (index >= AUD_SFZ_NUM_PARAMS) return;
  value = clampParam(index, value);
  node.params[index] = value;
  Shared& shared = *node.shared;
  switch (index) {
    case AUD_SFZ_PARAM_VOLUME:
      setGainTarget(node, value, rampFrames);
      return;
    case AUD_SFZ_PARAM_POLYPHONY:
      reloadIfChanged(node, shared.polyphony, stepOf(value));
      return;
    case AUD_SFZ_PARAM_TUNING_FREQUENCY:
      reloadIfChanged(node, shared.tuningFrequency, value);
      return;
    case AUD_SFZ_PARAM_STRETCH_TUNING:
      reloadIfChanged(node, shared.stretchTuning, value);
      return;
    case AUD_SFZ_PARAM_MAX_VOICES:
      // limitVoices reads it at the end of the block, over both synths: no
      // synth and no load takes it.
      return;
    case AUD_SFZ_PARAM_SAMPLE_QUALITY:
      shared.sampleQuality.store(stepOf(value), std::memory_order_release);
      break;
    case AUD_SFZ_PARAM_OSCILLATOR_QUALITY:
      shared.oscillatorQuality.store(stepOf(value), std::memory_order_release);
      break;
    default:
      return;
  }
  if (node.active != nullptr) applyRealtimeParams(node, *node.active);
  if (node.fading != nullptr) applyRealtimeParams(node, *node.fading);
}

// A controller an event sets, which a new synth takes over: a CC, the
// pitch bend, the channel pressure or the program.
struct Controller {
  enum class Kind { none, cc, pitchBend, pressure, program };
  Kind kind = Kind::none;
  uint32_t number = 0;  // the CC or the program
  float value = 0;      // the CC 0 to 1, the bend -1 to 1, the pressure 0 to 1
};

float unit7(uint32_t value) { return static_cast<float>(value & kDataMask) / kMax7Bit; }

// The controller of a UMP of the MIDI 1.0 or 2.0 channel voice protocol.
Controller controllerOfUmp(const AudEvent& event) {
  const uint32_t w0 = event.words[0];
  const uint32_t w1 = event.words[1];
  const uint32_t type = aud_ump_message_type(w0);
  const bool midi2 = type == AUD_UMP_TYPE_MIDI2_CHANNEL_VOICE;
  Controller controller;
  if (!midi2 && type != AUD_UMP_TYPE_MIDI1_CHANNEL_VOICE) return controller;
  switch (aud_ump_status(w0)) {
    case AUD_UMP_STATUS_CONTROL_CHANGE:
      controller.kind = Controller::Kind::cc;
      controller.number = aud_ump_note(w0);
      controller.value = midi2 ? aud_ump_unit_value(w1) : unit7(aud_ump_midi1_data2(w0));
      break;
    case AUD_UMP_STATUS_PROGRAM_CHANGE:
      controller.kind = Controller::Kind::program;
      controller.number =
          midi2 ? (w1 >> kMidi2ProgramShift) & kDataMask : aud_ump_midi1_data1(w0) & kDataMask;
      break;
    case AUD_UMP_STATUS_CHANNEL_PRESSURE:
      controller.kind = Controller::Kind::pressure;
      controller.value = midi2 ? aud_ump_unit_value(w1) : unit7(aud_ump_midi1_data1(w0));
      break;
    case AUD_UMP_STATUS_PITCH_BEND:
      controller.kind = Controller::Kind::pitchBend;
      if (midi2) {
        controller.value = 2.0f * aud_ump_unit_value(w1) - 1.0f;
      } else {
        const int bits = static_cast<int>((aud_ump_midi1_data2(w0) & kDataMask) << 7 |
                                          (aud_ump_midi1_data1(w0) & kDataMask));
        controller.value =
            static_cast<float>(bits - kPitchBendCentre) / static_cast<float>(kPitchBendCentre);
      }
      break;
    default:
      break;
  }
  return controller;
}

// The CC of a numeric control: the control id names a CC of sfizz, 0 to
// 511; an integer argument counts in steps of 1/127.
Controller controllerOfControl(const AudEvent& event) {
  Controller controller;
  if (event.words[0] >= kNumCcs) return controller;
  float value;
  switch (event.words[1]) {
    case AUD_ARG_FLOAT:
      std::memcpy(&value, &event.words[2], sizeof(float));
      break;
    case AUD_ARG_INT32:
      value = static_cast<float>(static_cast<int32_t>(event.words[2])) / kMax7Bit;
      break;
    case AUD_ARG_TRUE:
      value = 1.0f;
      break;
    case AUD_ARG_FALSE:
      value = 0.0f;
      break;
    default:
      return controller;
  }
  controller.kind = Controller::Kind::cc;
  controller.number = event.words[0];
  controller.value = std::min(1.0f, std::max(0.0f, value));
  return controller;
}

Controller controllerOf(const AudEvent& event) {
  if (event.type == AUD_EVENT_UMP) return controllerOfUmp(event);
  if (event.type == AUD_EVENT_CONTROL) return controllerOfControl(event);
  return Controller{};
}

// Remembers a controller for the synth that comes next.
void remember(Node& node, const Controller& controller) {
  switch (controller.kind) {
    case Controller::Kind::cc:
      node.cc[controller.number] = controller.value;
      node.ccSet[controller.number] = true;
      break;
    case Controller::Kind::pitchBend:
      node.pitchBend = controller.value;
      node.pitchBendSet = true;
      break;
    case Controller::Kind::pressure:
      node.aftertouch = controller.value;
      node.aftertouchSet = true;
      break;
    case Controller::Kind::program:
      node.program = static_cast<int>(controller.number);
      break;
    case Controller::Kind::none:
      break;
  }
}

// Sends a controller to the synth that plays.
void send(sfz::Synth& synth, int delay, const Controller& controller) {
  switch (controller.kind) {
    case Controller::Kind::cc:
      synth.hdcc(delay, static_cast<int>(controller.number), controller.value);
      break;
    case Controller::Kind::pitchBend:
      synth.hdPitchWheel(delay, controller.value);
      break;
    case Controller::Kind::pressure:
      synth.hdChannelAftertouch(delay, controller.value);
      break;
    case Controller::Kind::program:
      synth.programChange(delay, static_cast<int>(controller.number));
      break;
    case Controller::Kind::none:
      break;
  }
}

// The notes and the poly pressure of a UMP of the MIDI 1.0 or 2.0 channel
// voice protocol. sfizz knows one per-note controller, poly pressure; the
// other per-note messages of MIDI 2.0 are counted and reported (decision 6
// of the plan review).
void dispatchUmp(Node& node, sfz::Synth& synth, const AudEvent& event) {
  const uint32_t w0 = event.words[0];
  const uint32_t w1 = event.words[1];
  const uint32_t type = aud_ump_message_type(w0);
  if (type != AUD_UMP_TYPE_MIDI1_CHANNEL_VOICE && type != AUD_UMP_TYPE_MIDI2_CHANNEL_VOICE) {
    return;
  }
  const bool midi2 = type == AUD_UMP_TYPE_MIDI2_CHANNEL_VOICE;
  const int delay = static_cast<int>(event.sample_offset);
  const int note = static_cast<int>(aud_ump_note(w0));
  switch (aud_ump_status(w0)) {
    case AUD_UMP_STATUS_NOTE_ON:
      if (aud_ump_is_note_on(w0, w1)) {
        synth.hdNoteOn(delay, note, aud_ump_note_velocity(w0, w1));
      } else {
        synth.hdNoteOff(delay, note, 0.0f);
      }
      break;
    case AUD_UMP_STATUS_NOTE_OFF:
      synth.hdNoteOff(delay, note, aud_ump_note_velocity(w0, w1));
      break;
    case AUD_UMP_STATUS_POLY_PRESSURE:
      synth.hdPolyAftertouch(delay, note,
                             midi2 ? aud_ump_unit_value(w1) : unit7(aud_ump_midi1_data2(w0)));
      break;
    default:
      // Per-note controllers, per-note pitch bend, per-note management and
      // the registered controllers of MIDI 2.0: sfizz has no counterpart.
      if (midi2) node.unsupported += 1;
      break;
  }
}

// A message of the engine (osc-001) reaches sfizz's message API; sfizz
// answers through onReceive below, on this thread. sfizz's setters that
// take a string or a blob assign strings and parse them, which allocates:
// a message with such an argument, or with more arguments than any path
// of sfizz takes, is counted as unsupported and reported.
void dispatchMessage(Node& node, sfz::Synth& synth, const AudEvent& event) {
  const AudMessage* message = aud_event_message(&event);
  if (message == nullptr || message->path == nullptr) return;
  if (message->num_args > kMaxMessageArgs) {
    node.unsupported += 1;
    return;
  }
  for (uint32_t i = 0; i < message->num_args; ++i) {
    const uint32_t type = message->args[i].type;
    if (type == AUD_ARG_STRING || type == AUD_ARG_BLOB) {
      node.unsupported += 1;
      return;
    }
  }
  char sig[kMaxMessageArgs + 1];
  sfizz_arg_t args[kMaxMessageArgs];
  const uint32_t count = message->num_args;
  for (uint32_t i = 0; i < count; ++i) {
    const AudMessageArg& arg = message->args[i];
    sig[i] = static_cast<char>(arg.type);
    args[i].h = 0;
    switch (arg.type) {
      case AUD_ARG_INT32:
        args[i].i = arg.value.i;
        break;
      case AUD_ARG_INT64:
        args[i].h = arg.value.h;
        break;
      case AUD_ARG_FLOAT:
        args[i].f = arg.value.f;
        break;
      case AUD_ARG_DOUBLE:
        args[i].d = arg.value.d;
        break;
      case AUD_ARG_TRUE:
      case AUD_ARG_FALSE:
      case AUD_ARG_NIL:
      case AUD_ARG_IMPULSE:
        break;
      default:
        sig[i] = AUD_ARG_NIL;
        break;
    }
  }
  sig[count] = '\0';
  synth.dispatchMessage(*static_cast<sfz::Client*>(node.client),
                        static_cast<int>(event.sample_offset), message->path, sig, args);
}

// An event for the synth that plays: a controller, a note, a message.
void dispatch(Node& node, sfz::Synth& synth, const AudEvent& event,
              const Controller& controller) {
  if (controller.kind != Controller::Kind::none) {
    send(synth, static_cast<int>(event.sample_offset), controller);
  } else if (event.type == AUD_EVENT_UMP) {
    dispatchUmp(node, synth, event);
  } else if (event.type == AUD_EVENT_MESSAGE) {
    dispatchMessage(node, synth, event);
  }
}

// The events of the block in their order. Every controller is remembered
// for the synth that comes next, also before the first one; without a
// synth, nothing answers a message, which counts as unsupported. The
// volume ramps in applyVolume.
void dispatchEvents(Node& node, const AudProcessContext* context) {
  for (uint32_t i = 0; i < context->num_events; ++i) {
    const AudEvent& event = context->events[i];
    if (event.type == AUD_EVENT_PARAM) {
      if (event.words[0] != AUD_SFZ_PARAM_VOLUME) {
        applyParam(node, event.words[0], aud_event_param_value(&event), event.words[2]);
      }
      continue;
    }
    const Controller controller = controllerOf(event);
    remember(node, controller);
    if (node.active != nullptr) {
      dispatch(node, *node.active, event, controller);
    } else if (event.type == AUD_EVENT_MESSAGE) {
      node.unsupported += 1;
    }
  }
}

// sfizz's answer to a message, on the audio thread: it leaves through the
// engine's reply ring.
void onReceive(void* data, int, const char* path, const char* sig, const sfizz_arg_t* args) {
  Node& node = *static_cast<Node*>(data);
  AudMessageArg out[kMaxReplyArgs];
  uint32_t count = 0;
  for (; sig != nullptr && sig[count] != '\0' && count < kMaxReplyArgs; ++count) {
    AudMessageArg& arg = out[count];
    arg = AudMessageArg{};
    arg.struct_size = sizeof(AudMessageArg);
    arg.type = static_cast<uint32_t>(sig[count]);
    const sfizz_arg_t& value = args[count];
    switch (sig[count]) {
      case 'i':
        arg.value.i = value.i;
        break;
      case 'h':
        arg.value.h = value.h;
        break;
      case 'f':
        arg.value.f = value.f;
        break;
      case 'd':
        arg.value.d = value.d;
        break;
      case 's':
        arg.value.s = value.s;
        break;
      case 'b':
        arg.size = value.b != nullptr ? value.b->size : 0;
        arg.value.b = value.b != nullptr ? value.b->data : nullptr;
        break;
      case 'm':
        // A MIDI message of four bytes travels as a blob.
        arg.type = AUD_ARG_BLOB;
        arg.size = kMidiMessageBytes;
        arg.value.b = value.m;
        break;
      case 'T':
      case 'F':
      case 'N':
      case 'I':
        break;
      default:
        arg.type = AUD_ARG_NIL;
        break;
    }
  }
  const AudMessage message{sizeof(AudMessage), count, path, out};
  node.host->emit_message(node.host->host, &node, &message);
}

// Sends what changed of the block's transport to sfizz: tempo, time
// signature, playing state and position, for its tempo-synced modulation.
void sendTransport(Node& node, sfz::Synth& synth, const AudTransportSnapshot* transport) {
  if (transport == nullptr || transport->num_segments == 0 || transport->segments == nullptr) {
    return;
  }
  const AudTransportSegment& segment = transport->segments[0];
  if (segment.tempo > 0 && segment.tempo != node.tempo) {
    node.tempo = segment.tempo;
    synth.bpmTempo(0, static_cast<float>(segment.tempo));
  }
  if (segment.time_signature_numerator > 0 && segment.time_signature_denominator > 0 &&
      (segment.time_signature_numerator != node.numerator ||
       segment.time_signature_denominator != node.denominator)) {
    node.numerator = segment.time_signature_numerator;
    node.denominator = segment.time_signature_denominator;
    synth.timeSignature(0, static_cast<int>(node.numerator), static_cast<int>(node.denominator));
  }
  const int playing = (segment.flags & AUD_SEGMENT_PLAYING) ? 1 : 0;
  if (playing != node.playing) {
    node.playing = playing;
    synth.playbackState(0, playing);
  }
  if (playing != 0 && node.numerator > 0 && node.denominator > 0) {
    constexpr double kQuartersPerWhole = 4.0;
    const double beats = static_cast<double>(segment.beat) / static_cast<double>(AUD_BEAT_FACTOR);
    const double perBar = node.numerator * kQuartersPerWhole / node.denominator;
    const double bar = std::floor(beats / perBar);
    synth.timePosition(0, static_cast<int>(bar), beats - bar * perBar);
  }
}

// ............................................................................
// The voice limit and the budget of the process (ticket 29)

// The voices of the node: of the synth that plays and of the one that fades
// out.
sfz::VoiceCount countVoices(const Node& node) {
  sfz::VoiceCount total;
  const sfz::Synth* synths[2] = {node.fading, node.active};
  for (const sfz::Synth* synth : synths) {
    if (synth == nullptr) continue;
    const sfz::VoiceCount count = synth->countVoices();
    total.sounding += count.sounding;
    total.released += count.released;
    total.rendered += count.rendered;
  }
  return total;
}

// Steals `count` voices, those of the synth that fades out first - they end
// with its 5 ms anyway -, and returns the sounding and the released ones it
// stole.
sfz::VoiceCount stealVoices(Node& node, int32_t count) {
  sfz::VoiceCount stolen;
  sfz::Synth* synths[2] = {node.fading, node.active};
  for (sfz::Synth* synth : synths) {
    if (synth == nullptr || stolen.sounding >= count) continue;
    const sfz::VoiceCount taken =
        synth->stealVoices(count - stolen.sounding, static_cast<int>(node.stealFrames));
    stolen.sounding += taken.sounding;
    stolen.released += taken.released;
  }
  return stolen;
}

// The voices the node steals: those beyond max_voices and, in a live render,
// its share of the excess over the budget of the process.
int32_t stealCountOf(const Node& node, const VoiceTally& own, const VoiceBudget& budget) {
  const VoiceProcess process = budget.processFor(node.published);
  const int32_t maxVoices = stepOf(node.params[AUD_SFZ_PARAM_MAX_VOICES]);
  int32_t count = limitExcess(own, maxVoices, process.reserve);
  if (!node.freewheeling) count = std::max(count, budgetShare(own, process));
  return std::min(count, own.sounding);
}

// Keeps the voices below max_voices and the budget after the events of the
// block, so that the fades of the stolen voices start with it, and publishes
// what sounds then. An offline render publishes and counts nothing.
void limitVoices(Node& node, uint32_t frames) {
  VoiceBudget& budget = VoiceBudget::get();
  const sfz::VoiceCount count = countVoices(node);
  VoiceTally own;
  own.sounding = count.sounding;
  own.released = count.released;
  const int32_t steal = stealCountOf(node, own, budget);
  const sfz::VoiceCount stolen = steal > 0 ? stealVoices(node, steal) : sfz::VoiceCount();
  own.sounding -= stolen.sounding;
  own.released -= stolen.released;
  if (node.freewheeling) {
    budget.withdraw(node.published);
    return;
  }
  budget.publish(node.published, own, static_cast<uint32_t>(stolen.sounding),
                 static_cast<uint64_t>(count.rendered) * frames);
}

void clearBus(const AudAudioBus& bus, uint32_t frames) {
  for (uint32_t c = 0; c < bus.num_channels; ++c) {
    std::memset(bus.channels[c], 0, sizeof(float) * frames);
  }
}

// Renders the active synth into the output bus, the fading one under it
// with a falling gain, and the volume over both.
void render(Node& node, const AudProcessContext* context) {
  const AudAudioBus& out = context->outputs[0];
  const uint32_t frames = context->frames;
  const uint32_t channels = std::min(out.num_channels, node.channels);
  for (uint32_t c = 0; c < channels; ++c) node.outputPointers[c] = out.channels[c];
  sfz::AudioSpan<float> span(node.outputPointers.data(), channels, 0, frames);
  node.active->renderBlock(span);
  if (node.fading != nullptr && node.fadeRemaining > 0 && node.fadeFrames > 0) {
    sfz::AudioSpan<float> old(node.scratchPointers.data(), channels, 0, frames);
    node.fading->renderBlock(old);
    const uint32_t done = node.fadeFrames - node.fadeRemaining;
    for (uint32_t c = 0; c < channels; ++c) {
      float* target = out.channels[c];
      const float* source = node.scratchPointers[c];
      for (uint32_t i = 0; i < frames; ++i) {
        const uint32_t position = done + i;
        if (position >= node.fadeFrames) break;
        const float gain =
            1.0f - static_cast<float>(position) / static_cast<float>(node.fadeFrames);
        target[i] += source[i] * gain;
      }
    }
    node.fadeRemaining -= std::min(frames, node.fadeRemaining);
  }
  if (node.fading != nullptr && node.fadeRemaining == 0) {
    sfz::Synth* done = node.fading;
    node.fading = nullptr;
    retire(node, done);
  }
}

// The volume over the block, sample by sample: parameter events of the
// volume start their ramps at their offsets.
void applyVolume(Node& node, const AudProcessContext* context) {
  const AudAudioBus& out = context->outputs[0];
  const uint32_t frames = context->frames;
  uint32_t nextEvent = 0;
  const auto nextVolumeEvent = [&]() {
    while (nextEvent < context->num_events) {
      const AudEvent& event = context->events[nextEvent];
      if (event.type == AUD_EVENT_PARAM && event.words[0] == AUD_SFZ_PARAM_VOLUME) {
        return true;
      }
      nextEvent += 1;
    }
    return false;
  };
  bool pending = nextVolumeEvent();
  for (uint32_t i = 0; i < frames; ++i) {
    while (pending && context->events[nextEvent].sample_offset <= i) {
      const AudEvent& event = context->events[nextEvent];
      node.params[AUD_SFZ_PARAM_VOLUME] =
          clampParam(AUD_SFZ_PARAM_VOLUME, aud_event_param_value(&event));
      setGainTarget(node, node.params[AUD_SFZ_PARAM_VOLUME],
                    std::max<uint32_t>(1, event.words[2]));
      nextEvent += 1;
      pending = nextVolumeEvent();
    }
    if (node.gainRemaining > 0) {
      node.gain += node.gainStep;
      if (--node.gainRemaining == 0) node.gain = node.gainTarget;
    }
    if (node.gain == 1.0f) continue;
    for (uint32_t c = 0; c < out.num_channels; ++c) out.channels[c][i] *= node.gain;
  }
}

// The diagnostics of the block (lifecycle-001): voices that outran their
// stream, streams no worker took, messages and per-note messages sfizz
// cannot take.
void report(Node& node) {
  const AudHostApi* host = node.host;
  sfz::Synth* synths[2] = {node.active, node.fading};
  for (sfz::Synth* synth : synths) {
    if (synth == nullptr) continue;
    sfz::FilePool& pool = synth->getResources().getFilePool();
    const uint32_t underflows = pool.takeUnderflows();
    if (underflows > 0) host->report(host->host, &node, AUD_ERROR_UNDERFLOW, underflows);
    const uint32_t failures = pool.takeStreamFailures();
    if (failures > 0) host->report(host->host, &node, AUD_ERROR_QUEUE_FULL, failures);
  }
  if (node.unsupported > 0) {
    host->report(host->host, &node, AUD_ERROR_UNSUPPORTED, node.unsupported);
    node.unsupported = 0;
  }
}

// ............................................................................
// Creation

// Whether a host offers what the node calls: the tasks, the loads and the
// cache, and on the audio thread the reports and the messages.
bool complete(const AudHostApi* host) {
  return host != nullptr && host->struct_size >= sizeof(AudHostApi) &&
         host->post_task != nullptr && host->begin_load != nullptr &&
         host->load_progress != nullptr && host->end_load != nullptr &&
         host->load_cancelled != nullptr && host->register_cache != nullptr &&
         host->cache_changed != nullptr && host->cache_budget != nullptr &&
         host->report != nullptr && host->emit_message != nullptr;
}

// Puts the store of the process under the engine's sample cache budget;
// false, logged, when the engine takes no more caches.
bool joinBudget(const AudHostApi* host) {
  const int32_t result = host->register_cache(host->host, &kStoreVTable, &sfz::SampleStore::get());
  if (result == AUD_OK) return true;
  char message[96];
  std::snprintf(message, sizeof(message),
                "aud.sampler.sfz: the sample store cannot join the sample cache budget (%d)",
                static_cast<int>(result));
  logError(host, message);
  return false;
}

// A node of `host`; throws when the memory is not there.
Node* newNode(const AudHostApi* host) {
  std::unique_ptr<Node> node(new Node());
  node->host = host;
  node->shared = std::make_shared<Shared>();
  node->shared->host = host;
  node->outputPointers.assign(sfz::config::maxChannels, nullptr);
  auto* client = new sfz::Client(node.get());
  client->setReceiveCallback(&onReceive);
  node->client = client;
  return node.release();
}

}  // namespace

// ############################################################################
// The node

Node::Node() {
  for (uint32_t i = 0; i < AUD_SFZ_NUM_PARAMS; ++i) params[i] = kParams[i].default_value;
  gain = gainOf(params[AUD_SFZ_PARAM_VOLUME]);
  gainTarget = gain;
}

// ############################################################################
// The vtable

void* create(const AudNodeDescriptor*, const AudHostApi* host) {
  if (!complete(host) || !joinBudget(host)) return nullptr;
  try {
    return newNode(host);
  } catch (const std::exception&) {
    return nullptr;
  }
}

void destroy(void* instance) {
  auto* node = static_cast<Node*>(instance);
  Shared& shared = *node->shared;
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    shared.destroyed = true;
  }
  // A load that runs sees the end and keeps its synth to itself.
  shared.serial.fetch_add(1, std::memory_order_acq_rel);
  // The voices of the node no longer count in the budget of the process.
  VoiceBudget::get().withdraw(node->published);
  delete shared.ready.exchange(nullptr, std::memory_order_acq_rel);
  delete node->active;
  delete node->fading;
  for (sfz::Synth* synth : node->retired) delete synth;
  delete static_cast<sfz::Client*>(node->client);
  delete node;
}

int32_t prepare(void* instance, const AudPrepareInfo* info) {
  auto* node = static_cast<Node*>(instance);
  if (info == nullptr || info->num_output_buses != 1 || info->output_channels == nullptr) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  const uint32_t channels = info->output_channels[0];
  if (channels == 0 || channels % 2 != 0 || channels > sfz::config::maxChannels) {
    return AUD_ERROR_FORMAT;
  }
  // The block of the fading synth, allocated before anything changes.
  std::vector<float> scratch;
  std::vector<float*> scratchPointers;
  try {
    scratch.assign(static_cast<size_t>(channels) * info->max_frames, 0.0f);
    scratchPointers.resize(channels);
  } catch (const std::exception&) {
    return AUD_ERROR_OUT_OF_MEMORY;
  }
  for (uint32_t c = 0; c < channels; ++c) {
    scratchPointers[c] = scratch.data() + static_cast<size_t>(c) * info->max_frames;
  }
  Shared& shared = *node->shared;
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.config.sampleRate = info->sample_rate;
  shared.config.maxFrames = info->max_frames;
  shared.config.channels = channels;
  shared.config.generation += 1;
  const Config config = snapshot(shared);
  node->channels = channels;
  node->scratch.swap(scratch);
  node->scratchPointers.swap(scratchPointers);
  node->fadeFrames = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::lround(kCrossfadeSeconds * info->sample_rate)));
  // A fade that runs goes on at the new rate.
  node->fadeRemaining = std::min(node->fadeRemaining, node->fadeFrames);
  node->stealFrames = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::lround(kStealSeconds * info->sample_rate)));
  // No block renders now: the synths take the format, the one a load
  // handed over included.
  sfz::Synth* synths[3] = {node->active, node->fading,
                           shared.ready.load(std::memory_order_acquire)};
  for (sfz::Synth* synth : synths) {
    if (synth == nullptr) continue;
    synth->setSampleRate(static_cast<float>(config.sampleRate));
    synth->setSamplesPerBlock(static_cast<int>(config.maxFrames));
  }
  shared.built.sampleRate = config.sampleRate;
  shared.built.maxFrames = config.maxFrames;
  shared.built.channels = config.channels;
  shared.built.generation = config.generation;
  return AUD_OK;
}

void reset(void* instance, uint32_t) {
  auto* node = static_cast<Node*>(instance);
  if (node->active != nullptr) node->active->allSoundOff();
  if (node->fading != nullptr) node->fading->allSoundOff();
  // Its voices are gone; a stopped graph keeps none in the budget.
  VoiceBudget::get().withdraw(node->published);
}

void setParam(void* instance, uint32_t index, float value) {
  applyParam(*static_cast<Node*>(instance), index, value, kVolumeRampFrames);
}

void process(void* instance, const AudProcessContext* context) {
  Node& node = *static_cast<Node*>(instance);
  postRetired(node);
  adoptReady(node);
  followFreewheeling(node, context);
  dispatchEvents(node, context);
  if (context->num_output_buses == 0 || context->outputs == nullptr) {
    // Nothing renders: no voice of the node counts in the budget.
    VoiceBudget::get().withdraw(node.published);
    return;
  }
  limitVoices(node, context->frames);
  if (node.active == nullptr) {
    clearBus(context->outputs[0], context->frames);
  } else {
    sendTransport(node, *node.active, context->transport);
    render(node, context);
  }
  applyVolume(node, context);
  report(node);
}

int32_t setString(void* instance, uint32_t key, const char* value) {
  auto* node = static_cast<Node*>(instance);
  Setting setting;
  try {
    setting = changeSource(*node->shared, key, value);
  } catch (const std::exception&) {
    return AUD_ERROR_OUT_OF_MEMORY;
  }
  return setting.loads ? startLoad(*node, setting.name) : setting.result;
}

uint32_t getTail(void* instance) {
  Shared& shared = *static_cast<Node*>(instance)->shared;
  std::lock_guard<std::mutex> lock(shared.mutex);
  // sfizz takes releases longer than a tail counts, e.g. ampeg_release=1e30.
  const double frames =
      std::ceil(static_cast<double>(shared.tailSeconds) * shared.config.sampleRate);
  if (!(frames < static_cast<double>(kLongestTail))) return kLongestTail;
  return frames > 0 ? static_cast<uint32_t>(frames) : 0;
}

// ############################################################################
// The store as a cache of the engine

uint64_t trimStore(void* cache, uint64_t bytes) {
  return static_cast<sfz::SampleStore*>(cache)->trim(bytes);
}

const AudCacheVTable kStoreVTable = {sizeof(AudCacheVTable), trimStore};

}  // namespace aud_sfz

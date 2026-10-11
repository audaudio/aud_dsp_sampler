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
#include <new>

#include "aud_ump.h"
#include "FilePool.h"
#include "Messaging.h"
#include "Region.h"
#include "Synth.h"

namespace aud_sfz {
namespace {

// ............................................................................
// The engine as the host of the pools (ticket 25)

bool postTask(void* context, void (*run)(void*), void* user) {
  const auto* host = static_cast<const AudHostApi*>(context);
  const AudTask task{sizeof(AudTask), 0, run, user};
  return host->post_task(host->host, &task) == AUD_OK;
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
  config.oscillatorQuality =
      shared.oscillatorQuality.load(std::memory_order_acquire);
  config.tuningFrequency = shared.tuningFrequency.load(std::memory_order_acquire);
  config.stretchTuning = shared.stretchTuning.load(std::memory_order_acquire);
  return config;
}

bool sameSynthConfig(const Config& a, const Config& b) {
  return a.generation == b.generation && a.polyphony == b.polyphony &&
         a.sampleQuality == b.sampleQuality &&
         a.oscillatorQuality == b.oscillatorQuality &&
         a.tuningFrequency == b.tuningFrequency &&
         a.stretchTuning == b.stretchTuning;
}

// The longest amplitude release of an instrument at full velocity, in
// seconds.
float releaseOf(const sfz::Synth& synth) {
  float longest = 0;
  for (int i = 0, count = synth.getNumRegions(); i < count; ++i) {
    const sfz::Region* region = synth.getRegionView(i);
    if (region == nullptr) continue;
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

// What a load could not read, e.g. "sfizz could not read 2 samples: a.wav,
// b.wav"; empty when it read everything.
// A size for a message, e.g. "32 KB" or "1.5 MB".
std::string sizeText(uint64_t bytes) {
  char text[32];
  if (bytes < (1u << 20)) {
    std::snprintf(text, sizeof(text), "%llu KB",
                  static_cast<unsigned long long>((bytes + 1023) / 1024));
  } else {
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1 << 20));
  }
  return text;
}

std::string missingOf(const sfz::Synth& synth) {
  return listOf("sfizz could not read", "sample", "samples", synth.getMissingSamples());
}

// What a load that ended well could not do: the samples it could not read
// and the effects it does not know, which pass the signal through.
std::string notesOf(const sfz::Synth& synth) {
  std::string text = missingOf(synth);
  const std::string effects = listOf("sfizz does not know", "effect type",
                                     "effect types", synth.getUnsupportedEffects());
  if (!effects.empty()) text += (text.empty() ? "" : "; ") + effects;
  return text;
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
  std::unique_ptr<sfz::Synth> synth(new (std::nothrow) sfz::Synth());
  if (!synth) {
    endLoad(job, AUD_ERROR_OUT_OF_MEMORY, nullptr);
    return;
  }
  configure(*synth, config);
  sfz::FilePool& pool = synth->getResources().getFilePool();
  pool.setHost(fileHostOf(host));
  pool.setLoadObserver([&job, host](float progress) {
    if (stopped(job)) return false;
    if (job.load > 0) host->load_progress(host->host, job.load, progress);
    return true;
  });

  bool loaded = true;
  std::string what;
  switch (source.kind) {
    case Source::Kind::file:
      what = source.file;
      loaded = synth->loadSfzFile(source.file);
      break;
    case Source::Kind::text:
      what = source.virtualPath;
      loaded = synth->loadSfzString(source.virtualPath, source.text);
      break;
    case Source::Kind::none:
      break;
  }
  if (loaded && !source.scalaFile.empty()) {
    what = source.scalaFile;
    loaded = synth->loadScalaFile(source.scalaFile);
  } else if (loaded && !source.scalaText.empty()) {
    what = "the Scala text";
    loaded = synth->loadScalaString(source.scalaText);
  }
  const bool cancelled = pool.loadCancelled() || stopped(job);
  pool.setLoadObserver(nullptr);
  if (cancelled) {
    endLoad(job, AUD_ERROR_CANCELLED, nullptr);
    return;
  }
  if (!loaded) {
    std::string message = "sfizz could not load " + what;
    const std::vector<std::string>& errors = synth->getParseErrors();
    if (!errors.empty()) message += ": " + errors.front();
    endLoad(job, AUD_ERROR_FAILED, message.c_str());
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
  const uint64_t pinned = synth->getResources().getFilePool().pinnedBytes();
  const uint64_t budget = host->cache_budget(host->host);
  if (pinned > budget) {
    const std::string message = "the samples of the instrument pin " + sizeText(pinned) +
                                ", more than the sample cache budget of " +
                                sizeText(budget);
    endLoad(job, AUD_ERROR_CAPACITY, message.c_str());
    return;
  }
  const float release = releaseOf(*synth);
  const std::string notes = notesOf(*synth);

  sfz::Synth* previous = nullptr;
  bool handed = false;
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    if (!shared.destroyed &&
        shared.serial.load(std::memory_order_acquire) == job.serial) {
      // A prepare or a parameter since the configuration: the synth does
      // not render yet, so it takes them now.
      const Config now = snapshot(shared);
      if (!sameSynthConfig(now, config)) configure(*synth, now);
      shared.built = now;
      shared.builtSource = source;
      shared.tailSeconds = release;
      previous = shared.ready.exchange(synth.release(), std::memory_order_acq_rel);
      handed = true;
    }
  }
  delete previous;  // a synth no block adopted
  if (!handed) {
    endLoad(job, AUD_ERROR_CANCELLED, nullptr);
  } else {
    endLoad(job, AUD_OK, notes.empty() ? nullptr : notes.c_str());
  }
}

void loadTask(void* user) {
  std::unique_ptr<LoadJob> job(static_cast<LoadJob*>(user));
  runLoad(*job);
}

void finishLoad(const std::shared_ptr<Shared>& shared);

// A change of the polyphony or the tuning loads the instrument again: sfizz
// sets its voices only while it does not render, and its tuning tables
// allocate. A load that runs takes the change when it hands its synth over,
// so the reload stands back then and the last load to end runs it. The
// flags are sequentially consistent: either the reload sees the load, or
// the load sees the deferred reload.
void reloadNow(const std::shared_ptr<Shared>& shared) {
  shared->reloadDeferred.store(true);
  if (shared->loads.load() > 0) return;
  if (!shared->reloadDeferred.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (shared->destroyed || shared->builtSource.kind == Source::Kind::none) return;
    if (sameSynthConfig(snapshot(*shared), shared->built)) return;
  }
  LoadJob job;
  job.shared = shared;
  job.serial = shared->serial.fetch_add(1, std::memory_order_acq_rel) + 1;
  shared->loads.fetch_add(1);
  runLoad(job);
  finishLoad(shared);
}

void finishLoad(const std::shared_ptr<Shared>& shared) {
  if (shared->loads.fetch_sub(1) == 1 && shared->reloadDeferred.load()) reloadNow(shared);
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
  const AudTask task{sizeof(AudTask), 0, reloadTask, &reload};
  if (host->post_task(host->host, &task) != AUD_OK) {
    reload.keep.reset();
    reload.posted.store(false, std::memory_order_release);
  }
}

void countedLoadTask(void* user) {
  std::shared_ptr<Shared> shared = static_cast<LoadJob*>(user)->shared;
  loadTask(user);
  finishLoad(shared);
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
  job->serial = node.shared->serial.fetch_add(1, std::memory_order_acq_rel) + 1;
  node.shared->loads.fetch_add(1, std::memory_order_acq_rel);
  const AudTask task{sizeof(AudTask), 0, countedLoadTask, job};
  const int32_t posted = host->post_task(host->host, &task);
  if (posted != AUD_OK) {
    if (node.shared->loads.fetch_sub(1) == 1 && node.shared->reloadDeferred.load()) {
      postReload(node.shared, host);
    }
    host->end_load(host->host, id, posted, "no worker thread took the load");
    delete job;
    return posted;
  }
  return id;
}

// ............................................................................
// The audio thread

// Hands the synths the audio thread is done with to a worker; what the
// queue refuses waits for the next block.
void postRetired(Node& node) {
  for (sfz::Synth*& synth : node.retired) {
    if (synth == nullptr) continue;
    const AudTask task{sizeof(AudTask), 0, deleteSynth, synth};
    if (node.host->post_task(node.host->host, &task) == AUD_OK) synth = nullptr;
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
                         static_cast<int>(node.params[AUD_SFZ_PARAM_SAMPLE_QUALITY]));
  synth.setOscillatorQuality(
      sfz::Synth::ProcessLive,
      static_cast<int>(node.params[AUD_SFZ_PARAM_OSCILLATOR_QUALITY]));
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

void setCc(Node& node, uint32_t cc, float value) {
  if (cc >= kNumCcs) return;
  node.cc[cc] = value;
  node.ccSet[cc] = true;
}

float unit7(uint32_t value) { return static_cast<float>(value & 0x7f) / 127.0f; }

// A UMP of the MIDI 1.0 or 2.0 channel voice protocol. sfizz knows one
// per-note controller, poly pressure; the other per-note messages of MIDI
// 2.0 are counted and reported (decision 6 of the plan review).
void dispatchUmp(Node& node, sfz::Synth& synth, const AudEvent& event) {
  const uint32_t w0 = event.words[0];
  const uint32_t w1 = event.words[1];
  const uint32_t type = aud_ump_message_type(w0);
  if (type != AUD_UMP_TYPE_MIDI1_CHANNEL_VOICE &&
      type != AUD_UMP_TYPE_MIDI2_CHANNEL_VOICE) {
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
                             midi2 ? aud_ump_unit_value(w1)
                                   : unit7(aud_ump_midi1_data2(w0)));
      break;
    case AUD_UMP_STATUS_CONTROL_CHANGE: {
      const uint32_t cc = aud_ump_note(w0);
      const float value =
          midi2 ? aud_ump_unit_value(w1) : unit7(aud_ump_midi1_data2(w0));
      setCc(node, cc, value);
      synth.hdcc(delay, static_cast<int>(cc), value);
      break;
    }
    case AUD_UMP_STATUS_PROGRAM_CHANGE: {
      const int program = static_cast<int>(
          midi2 ? (w1 >> 24) & 0x7f : aud_ump_midi1_data1(w0) & 0x7f);
      node.program = program;
      synth.programChange(delay, program);
      break;
    }
    case AUD_UMP_STATUS_CHANNEL_PRESSURE: {
      const float value =
          midi2 ? aud_ump_unit_value(w1) : unit7(aud_ump_midi1_data1(w0));
      node.aftertouch = value;
      node.aftertouchSet = true;
      synth.hdChannelAftertouch(delay, value);
      break;
    }
    case AUD_UMP_STATUS_PITCH_BEND: {
      float value;
      if (midi2) {
        value = 2.0f * aud_ump_unit_value(w1) - 1.0f;
      } else {
        const int bits = static_cast<int>((aud_ump_midi1_data2(w0) & 0x7f) << 7 |
                                          (aud_ump_midi1_data1(w0) & 0x7f));
        value = static_cast<float>(bits - 8192) / 8192.0f;
      }
      node.pitchBend = value;
      node.pitchBendSet = true;
      synth.hdPitchWheel(delay, value);
      break;
    }
    default:
      // Per-note controllers, per-note pitch bend, per-note management and
      // the registered controllers of MIDI 2.0: sfizz has no counterpart.
      if (midi2) node.unsupported += 1;
      break;
  }
}

// A numeric control: the control id names a CC of sfizz, 0 to 511.
void dispatchControl(Node& node, sfz::Synth& synth, const AudEvent& event) {
  const uint32_t cc = event.words[0];
  if (cc >= kNumCcs) return;
  float value;
  switch (event.words[1]) {
    case AUD_ARG_FLOAT: {
      std::memcpy(&value, &event.words[2], sizeof(float));
      break;
    }
    case AUD_ARG_INT32:
      value = static_cast<float>(static_cast<int32_t>(event.words[2])) / 127.0f;
      break;
    case AUD_ARG_TRUE:
      value = 1.0f;
      break;
    case AUD_ARG_FALSE:
      value = 0.0f;
      break;
    default:
      return;
  }
  value = std::min(1.0f, std::max(0.0f, value));
  setCc(node, cc, value);
  synth.hdcc(static_cast<int>(event.sample_offset), static_cast<int>(cc), value);
}

// A message of the engine (osc-001) reaches sfizz's message API; sfizz
// answers through onReceive below, on this thread. sfizz's setters that
// take a string or a blob assign strings and parse them, which allocates:
// a message with such an argument, or with more arguments than any path
// of sfizz takes, is counted as unsupported and reported.
void dispatchMessage(Node& node, sfz::Synth& synth, const AudEvent& event) {
  const AudMessage* message = aud_event_message(&event);
  if (message == nullptr || message->path == nullptr) return;
  constexpr uint32_t kMax = 4;
  if (message->num_args > kMax) {
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
  char sig[kMax + 1];
  sfizz_arg_t args[kMax];
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
                        static_cast<int>(event.sample_offset), message->path,
                        sig, args);
}

// sfizz's answer to a message, on the audio thread: it leaves through the
// engine's reply ring.
void onReceive(void* data, int, const char* path, const char* sig,
               const sfizz_arg_t* args) {
  Node& node = *static_cast<Node*>(data);
  constexpr uint32_t kMax = 16;
  AudMessageArg out[kMax];
  uint32_t count = 0;
  for (; sig != nullptr && sig[count] != '\0' && count < kMax; ++count) {
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
        arg.size = 4;
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
void sendTransport(Node& node, sfz::Synth& synth,
                   const AudTransportSnapshot* transport) {
  if (transport == nullptr || transport->num_segments == 0 ||
      transport->segments == nullptr) {
    return;
  }
  const AudTransportSegment& segment = transport->segments[0];
  if (segment.tempo > 0 && segment.tempo != node.tempo) {
    node.tempo = segment.tempo;
    synth.bpmTempo(0, static_cast<float>(segment.tempo));
  }
  if (segment.time_signature_numerator > 0 &&
      segment.time_signature_denominator > 0 &&
      (segment.time_signature_numerator != node.numerator ||
       segment.time_signature_denominator != node.denominator)) {
    node.numerator = segment.time_signature_numerator;
    node.denominator = segment.time_signature_denominator;
    synth.timeSignature(0, static_cast<int>(node.numerator),
                        static_cast<int>(node.denominator));
  }
  const int playing = (segment.flags & AUD_SEGMENT_PLAYING) ? 1 : 0;
  if (playing != node.playing) {
    node.playing = playing;
    synth.playbackState(0, playing);
  }
  if (playing != 0 && node.numerator > 0 && node.denominator > 0) {
    const double beats = static_cast<double>(segment.beat) /
                         static_cast<double>(AUD_BEAT_FACTOR);
    const double perBar = node.numerator * 4.0 / node.denominator;
    const double bar = std::floor(beats / perBar);
    synth.timePosition(0, static_cast<int>(bar), beats - bar * perBar);
  }
}

void setGainTarget(Node& node, float db, uint32_t rampFrames) {
  node.gainTarget = gainOf(db);
  node.gainRemaining = std::max<uint32_t>(1, rampFrames);
  node.gainStep = (node.gainTarget - node.gain) / static_cast<float>(node.gainRemaining);
}

float clampParam(uint32_t index, float value) {
  static const float kMin[AUD_SFZ_NUM_PARAMS] = {-60, 1, 0, 0, 400, 0};
  static const float kMax[AUD_SFZ_NUM_PARAMS] = {6, 256, 10, 3, 480, 1};
  return std::min(kMax[index], std::max(kMin[index], value));
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
    case AUD_SFZ_PARAM_POLYPHONY: {
      const int voices = static_cast<int>(std::lround(value));
      if (shared.polyphony.exchange(voices, std::memory_order_acq_rel) != voices) {
        postReload(node.shared, node.host);
      }
      return;
    }
    case AUD_SFZ_PARAM_TUNING_FREQUENCY:
      if (shared.tuningFrequency.exchange(value, std::memory_order_acq_rel) != value) {
        postReload(node.shared, node.host);
      }
      return;
    case AUD_SFZ_PARAM_STRETCH_TUNING:
      if (shared.stretchTuning.exchange(value, std::memory_order_acq_rel) != value) {
        postReload(node.shared, node.host);
      }
      return;
    case AUD_SFZ_PARAM_SAMPLE_QUALITY:
      shared.sampleQuality.store(static_cast<int>(value), std::memory_order_release);
      break;
    case AUD_SFZ_PARAM_OSCILLATOR_QUALITY:
      shared.oscillatorQuality.store(static_cast<int>(value),
                                     std::memory_order_release);
      break;
    default:
      return;
  }
  if (node.active != nullptr) applyRealtimeParams(node, *node.active);
  if (node.fading != nullptr) applyRealtimeParams(node, *node.fading);
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
        const float gain = 1.0f - static_cast<float>(position) /
                                      static_cast<float>(node.fadeFrames);
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
// stream, streams no worker took, per-note messages sfizz cannot play.
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

}  // namespace

// ############################################################################
// The vtable

void* create(const AudNodeDescriptor*, const AudHostApi* host) {
  if (host == nullptr || host->struct_size < sizeof(AudHostApi) ||
      host->post_task == nullptr || host->begin_load == nullptr) {
    return nullptr;
  }
  auto* node = new (std::nothrow) Node();
  if (node == nullptr) return nullptr;
  node->host = host;
  node->shared = std::make_shared<Shared>();
  node->shared->host = host;
  auto* client = new (std::nothrow) sfz::Client(node);
  if (client == nullptr) {
    delete node;
    return nullptr;
  }
  client->setReceiveCallback(&onReceive);
  node->client = client;
  node->outputPointers.assign(sfz::config::maxChannels, nullptr);
  host->register_cache(host->host, &kStoreVTable, &sfz::SampleStore::get());
  return node;
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
  delete shared.ready.exchange(nullptr, std::memory_order_acq_rel);
  delete node->active;
  delete node->fading;
  for (sfz::Synth* synth : node->retired) delete synth;
  delete static_cast<sfz::Client*>(node->client);
  delete node;
}

int32_t prepare(void* instance, const AudPrepareInfo* info) {
  auto* node = static_cast<Node*>(instance);
  if (info == nullptr || info->num_output_buses != 1 ||
      info->output_channels == nullptr) {
    return AUD_ERROR_INVALID_ARGUMENT;
  }
  const uint32_t channels = info->output_channels[0];
  if (channels == 0 || channels % 2 != 0 || channels > sfz::config::maxChannels) {
    return AUD_ERROR_FORMAT;
  }
  Shared& shared = *node->shared;
  std::lock_guard<std::mutex> lock(shared.mutex);
  shared.config.sampleRate = info->sample_rate;
  shared.config.maxFrames = info->max_frames;
  shared.config.channels = channels;
  shared.config.generation += 1;
  const Config config = snapshot(shared);
  node->channels = channels;
  node->scratch.assign(static_cast<size_t>(channels) * info->max_frames, 0.0f);
  node->scratchPointers.resize(channels);
  for (uint32_t c = 0; c < channels; ++c) {
    node->scratchPointers[c] = node->scratch.data() + c * info->max_frames;
  }
  node->fadeFrames = std::max<uint32_t>(
      1, static_cast<uint32_t>(std::lround(kCrossfadeSeconds * info->sample_rate)));
  // A fade that runs goes on at the new rate.
  node->fadeRemaining = std::min(node->fadeRemaining, node->fadeFrames);
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
}

void setParam(void* instance, uint32_t index, float value) {
  applyParam(*static_cast<Node*>(instance), index, value, kVolumeRampFrames);
}

void process(void* instance, const AudProcessContext* context) {
  Node& node = *static_cast<Node*>(instance);
  postRetired(node);
  // A synth a load handed over waits while a fade runs, at most 5 ms.
  if (node.fading == nullptr && hasFreeSlot(node)) {
    sfz::Synth* next =
        node.shared->ready.exchange(nullptr, std::memory_order_acq_rel);
    if (next != nullptr) adopt(node, next);
  }
  if (context->num_output_buses == 0 || context->outputs == nullptr) return;
  const AudAudioBus& out = context->outputs[0];
  const bool offline = (context->flags & AUD_PROCESS_OFFLINE) != 0;
  if (offline != node.freewheeling) {
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
  for (uint32_t i = 0; i < context->num_events; ++i) {
    const AudEvent& event = context->events[i];
    if (event.type == AUD_EVENT_PARAM && event.words[0] != AUD_SFZ_PARAM_VOLUME) {
      applyParam(node, event.words[0], aud_event_param_value(&event), event.words[2]);
      continue;
    }
    if (node.active == nullptr) continue;
    switch (event.type) {
      case AUD_EVENT_UMP:
        dispatchUmp(node, *node.active, event);
        break;
      case AUD_EVENT_CONTROL:
        dispatchControl(node, *node.active, event);
        break;
      case AUD_EVENT_MESSAGE:
        dispatchMessage(node, *node.active, event);
        break;
      default:
        break;
    }
  }
  if (node.active == nullptr) {
    clearBus(out, context->frames);
    return;
  }
  sendTransport(node, *node.active, context->transport);
  render(node, context);
  applyVolume(node, context);
  report(node);
}

int32_t setString(void* instance, uint32_t key, const char* value) {
  auto* node = static_cast<Node*>(instance);
  Shared& shared = *node->shared;
  const std::string text = value != nullptr ? value : "";
  std::string name;
  {
    std::lock_guard<std::mutex> lock(shared.mutex);
    Source& source = shared.source;
    switch (key) {
      case AUD_SFZ_KEY_FILE:
        source.kind = text.empty() ? Source::Kind::none : Source::Kind::file;
        source.file = text;
        name = text;
        break;
      case AUD_SFZ_KEY_VIRTUAL_PATH:
        // The path the next text resolves its samples against: a text and
        // its path come together, so the path starts no load of its own.
        source.virtualPath = text.empty() ? Source().virtualPath : text;
        return AUD_OK;
      case AUD_SFZ_KEY_TEXT:
        source.kind = text.empty() ? Source::Kind::none : Source::Kind::text;
        source.text = text;
        name = source.virtualPath;
        break;
      case AUD_SFZ_KEY_SCALA_FILE:
        source.scalaFile = text;
        source.scalaText.clear();
        if (source.kind == Source::Kind::none) return AUD_OK;
        name = text;
        break;
      case AUD_SFZ_KEY_SCALA_TEXT:
        source.scalaText = text;
        source.scalaFile.clear();
        if (source.kind == Source::Kind::none) return AUD_OK;
        name = "scala";
        break;
      default:
        return AUD_ERROR_INVALID_ARGUMENT;
    }
  }
  return startLoad(*node, name);
}

uint32_t getTail(void* instance) {
  Shared& shared = *static_cast<Node*>(instance)->shared;
  std::lock_guard<std::mutex> lock(shared.mutex);
  return static_cast<uint32_t>(
      std::ceil(static_cast<double>(shared.tailSeconds) * shared.config.sampleRate));
}

// ############################################################################
// The store as a cache of the engine

uint64_t trimStore(void* cache, uint64_t bytes) {
  return static_cast<sfz::SampleStore*>(cache)->trim(bytes);
}

const AudCacheVTable kStoreVTable = {sizeof(AudCacheVTable), trimStore};

}  // namespace aud_sfz

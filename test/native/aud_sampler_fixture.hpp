// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// What the native tests of the sampler share: a graph of the engine with
// the sampler registered, instruments written to a temporary folder from
// generated samples, the workers held at will, and the messages of the
// node unpacked.

#ifndef AUD_SAMPLER_FIXTURE_HPP
#define AUD_SAMPLER_FIXTURE_HPP

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "aud_audio_graph.h"
#include "aud_dsp_sampler.h"
#include "aud_test.hpp"
#include "aud_ump.h"

namespace aud_sampler_test {

inline constexpr double kRate = 48000.0;
inline constexpr double kPi = 3.14159265358979323846;
// The frames sfizz reads ahead of a voice: the head of a sample.
inline constexpr uint32_t kHeadFrames = 8192;

// ............................................................................
// Files

// A folder of its own for every test, removed at the end. The start time of
// the run tells runs apart, the counter the folders of one run.
struct TempFolder {
  std::filesystem::path path;

  TempFolder() {
    static const auto run = std::chrono::system_clock::now().time_since_epoch().count();
    static std::atomic<int> next{0};
    path = std::filesystem::temp_directory_path() /
           ("aud_sampler_test_" + std::to_string(run) + "_" +
            std::to_string(next.fetch_add(1)));
    std::filesystem::create_directories(path);
  }
  ~TempFolder() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::string file(const std::string& name) const { return (path / name).string(); }
};

// Writes a WAV file of 32-bit floats, interleaved.
inline void writeWav(const std::string& file, const std::vector<float>& samples,
                     uint32_t channels = 1, uint32_t rate = 48000) {
  std::ofstream out(file, std::ios::binary);
  const auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  const auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
  const uint32_t bytes = static_cast<uint32_t>(samples.size() * sizeof(float));
  out.write("RIFF", 4);
  u32(36 + bytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  u32(16);
  u16(3);  // IEEE float
  u16(static_cast<uint16_t>(channels));
  u32(rate);
  u32(rate * channels * 4);
  u16(static_cast<uint16_t>(channels * 4));
  u16(32);
  out.write("data", 4);
  u32(bytes);
  out.write(reinterpret_cast<const char*>(samples.data()), bytes);
}

inline void writeText(const std::string& file, const std::string& text) {
  std::ofstream out(file, std::ios::binary);
  out << text;
}

// A sine of `frequency` Hz, `seconds` long, at `amplitude`.
inline std::vector<float> sine(double frequency, double seconds,
                               float amplitude = 0.5f, uint32_t rate = 48000) {
  std::vector<float> samples(static_cast<size_t>(seconds * rate));
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = amplitude * static_cast<float>(std::sin(2 * kPi * frequency * i / rate));
  }
  return samples;
}

// ............................................................................
// The graph with the sampler

// The workers of the process, held: tasks posted meanwhile wait until the
// hold ends. Two blocking tasks take both workers. Nothing may wait for the
// workers while they are held: no offline render, no wait for a load.
struct WorkerHold {
  static std::atomic<int>& holding() {
    static std::atomic<int> value{0};
    return value;
  }
  static std::atomic<bool>& released() {
    static std::atomic<bool> value{false};
    return value;
  }
  static void block(void*) {
    holding().fetch_add(1);
    while (!released().load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    holding().fetch_sub(1);
  }

  bool active = true;

  explicit WorkerHold(const AudHostApi* host) {
    released().store(false);
    for (int i = 0; i < 2; ++i) {
      const AudTask task{sizeof(AudTask), 0, block, nullptr};
      host->post_task(host->host, &task);
    }
    while (holding().load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  void release() {
    if (!active) return;
    active = false;
    released().store(true);
    while (holding().load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ~WorkerHold() { release(); }
};

struct Fixture {
  AudGraph* graph = nullptr;
  uint32_t outChannels = 2;
  uint32_t maxFrames;
  double rate;
  std::vector<std::vector<float>> out;
  std::vector<float*> outPointers;
  AudAudioBus outBus{};
  // What the renders produced on channel 0, and the notifications taken.
  std::vector<float> left;
  std::vector<AudGraphNotification> notifications;
  // The engine's watchdog counts what the audio thread must not do, sfizz
  // included; every test ends without a violation.
  uint64_t violationsBefore = aud_graph_watchdog_violations();

  explicit Fixture(uint32_t frames = 256, double sampleRate = kRate)
      : maxFrames(frames), rate(sampleRate) {
    AudGraphConfig config{};
    config.struct_size = sizeof(AudGraphConfig);
    config.sample_rate = sampleRate;
    config.max_frames = frames;
    config.num_output_buses = 1;
    config.output_channels = &outChannels;
    graph = aud_graph_create(&config);
    AUD_CHECK(aud_dsp_sampler_register(aud_graph_host_api(graph)) == AUD_OK);
    out.assign(outChannels, std::vector<float>(frames, 0.0f));
    for (auto& channel : out) outPointers.push_back(channel.data());
    outBus = {sizeof(AudAudioBus), outChannels, outPointers.data()};
  }
  ~Fixture() {
    if (graph != nullptr) destroy();
  }

  void destroy() {
    takeNotifications();
    const uint64_t violations = aud_graph_watchdog_violations() - violationsBefore;
    AUD_CHECK(violations == 0);
    for (const AudGraphNotification& n : notifications) {
      if (n.type == AUD_NOTIFY_DIAGNOSTIC && n.code == AUD_GRAPH_ERROR_REALTIME_VIOLATION) {
        std::printf("  the watchdog caught %u violations of kinds 0x%llx\n", n.count,
                    static_cast<unsigned long long>(n.value));
      }
    }
    aud_graph_destroy(graph);
    graph = nullptr;
  }

  const AudHostApi* host() const { return aud_graph_host_api(graph); }

  AudGraphStats stats() {
    AudGraphStats result{};
    result.struct_size = sizeof(AudGraphStats);
    aud_graph_get_stats(graph, &result);
    return result;
  }

  // A sampler node, not connected yet.
  int32_t createSampler() {
    const int32_t node = aud_graph_create_node(graph, AUD_DSP_SAMPLER_SFZ_TYPE_ID, nullptr);
    AUD_CHECK(node > 0);
    return node;
  }

  // A sampler node connected to the output; the graph runs.
  int32_t sampler() {
    const int32_t node = createSampler();
    aud_graph_begin(graph);
    aud_graph_connect(graph, node, 0, AUD_GRAPH_NODE, 0, 0);
    AUD_CHECK(aud_graph_commit(graph) > 0);
    if (aud_graph_state(graph) != AUD_GRAPH_RUNNING) aud_graph_start(graph);
    return node;
  }

  // Commits an empty transaction: the graph reads the tails again.
  void commit() {
    aud_graph_begin(graph);
    AUD_CHECK(aud_graph_commit(graph) > 0);
  }

  void remove(int32_t node) {
    aud_graph_begin(graph);
    AUD_CHECK(aud_graph_remove_node(graph, node) == AUD_OK);
    AUD_CHECK(aud_graph_commit(graph) > 0);
  }

  // Loads SFZ text with samples next to `virtualPath`; returns the load id.
  int32_t loadText(int32_t node, const std::string& virtualPath, const std::string& text) {
    AUD_CHECK(aud_graph_set_string(graph, node, AUD_SFZ_KEY_VIRTUAL_PATH,
                                   virtualPath.c_str()) == AUD_OK);
    return aud_graph_set_string(graph, node, AUD_SFZ_KEY_TEXT, text.c_str());
  }

  // Loads an SFZ file; returns the load id.
  int32_t loadFile(int32_t node, const std::string& file) {
    return aud_graph_set_string(graph, node, AUD_SFZ_KEY_FILE, file.c_str());
  }

  AudLoadInfo load(int32_t id) {
    AudLoadInfo info{};
    info.struct_size = sizeof(AudLoadInfo);
    aud_graph_load_info(graph, id, &info);
    return info;
  }

  // Loads and waits; true when the load ended well.
  bool loadAndWait(int32_t node, const std::string& virtualPath, const std::string& text) {
    const int32_t id = loadText(node, virtualPath, text);
    AUD_CHECK(id > 0);
    AUD_CHECK(aud_graph_wait_loads(graph, node, 10000) == AUD_OK);
    return load(id).result == AUD_OK;
  }

  void param(int32_t node, uint32_t index, float value, uint32_t ramp = 0) {
    AUD_CHECK(aud_graph_set_param(graph, node, index, value, ramp) == AUD_OK);
  }

  // Renders offline: the workers finish what a block asked for before the
  // next one. Appends channel 0 to `left`.
  void renderOffline(uint32_t frames, uint32_t block = 256) {
    std::vector<std::vector<float>> buffers(outChannels, std::vector<float>(frames, 0.0f));
    std::vector<float*> pointers;
    for (auto& channel : buffers) pointers.push_back(channel.data());
    AudAudioBus bus{sizeof(AudAudioBus), outChannels, pointers.data()};
    AudOfflineRequest request{};
    request.struct_size = sizeof(AudOfflineRequest);
    request.frames = frames;
    request.block_frames = block;
    request.outputs = &bus;
    request.start_sample_position = aud_graph_sample_position(graph);
    AUD_CHECK(aud_graph_render_offline(graph, &request) == AUD_OK);
    left.insert(left.end(), buffers[0].begin(), buffers[0].end());
    takeNotifications();
  }

  // Renders like a stream: nothing waits for the workers.
  void renderLive(uint32_t frames) {
    uint32_t done = 0;
    while (done < frames) {
      const uint32_t block = std::min(maxFrames, frames - done);
      AudRenderRequest request{};
      request.struct_size = sizeof(AudRenderRequest);
      request.frames = block;
      request.num_output_buses = 1;
      request.outputs = &outBus;
      AUD_CHECK(aud_graph_render(graph, &request) == AUD_OK);
      left.insert(left.end(), out[0].begin(), out[0].begin() + block);
      done += block;
    }
    takeNotifications();
  }

  void takeNotifications() {
    AudGraphNotification buffer[64];
    while (true) {
      const int32_t count = aud_graph_take_notifications(graph, buffer, 64);
      for (int32_t i = 0; i < count; ++i) notifications.push_back(buffer[i]);
      if (count < 64) return;
    }
  }

  uint32_t diagnostics(int32_t code, int32_t node = 0) const {
    uint32_t count = 0;
    for (const AudGraphNotification& n : notifications) {
      if (n.type == AUD_NOTIFY_DIAGNOSTIC && n.code == code &&
          (node == 0 || n.node == node)) {
        count += n.count;
      }
    }
    return count;
  }

  // The notifications of a type, of one load when `load` is above 0.
  std::vector<AudGraphNotification> notificationsOf(uint32_t type, int64_t load = 0) const {
    std::vector<AudGraphNotification> result;
    for (const AudGraphNotification& n : notifications) {
      if (n.type == type && (load == 0 || n.value == load)) result.push_back(n);
    }
    return result;
  }

  void send(int32_t node, const AudEvent& event) {
    AUD_CHECK(aud_graph_send_event(graph, node, &event, nullptr, 0) == AUD_OK);
  }

  void noteOn(int32_t node, uint32_t note, uint32_t velocity = 100) {
    const uint32_t word = aud_ump_midi1_word(0, 0x90, note, velocity);
    send(node, aud_event_ump(&word, 1, 0, 0));
  }

  void noteOff(int32_t node, uint32_t note) {
    const uint32_t word = aud_ump_midi1_word(0, 0x80, note, 0);
    send(node, aud_event_ump(&word, 1, 0, 0));
  }

  void cc(int32_t node, uint32_t number, uint32_t value) {
    const uint32_t word = aud_ump_midi1_word(0, 0xB0, number, value);
    send(node, aud_event_ump(&word, 1, 0, 0));
  }

  // A pitch bend of MIDI 1.0: 0 to 16383, the centre at 8192.
  void bend(int32_t node, uint32_t value) {
    const uint32_t word = aud_ump_midi1_word(0, 0xE0, value & 0x7f, value >> 7);
    send(node, aud_event_ump(&word, 1, 0, 0));
  }
};

// Waits until the workers of the process are idle.
inline void waitTasks() { AUD_CHECK(aud_tasks_wait_idle(10000) == AUD_OK); }

inline AudCacheUsage cacheUsage() {
  AudCacheUsage usage{};
  usage.struct_size = sizeof(AudCacheUsage);
  AUD_CHECK(aud_cache_usage(&usage) == AUD_OK);
  return usage;
}

// A budget for one test; the default comes back at the end.
struct BudgetScope {
  uint64_t before = aud_cache_budget();
  explicit BudgetScope(uint64_t bytes) {
    aud_cache_set_budget(bytes);
    waitTasks();
  }
  ~BudgetScope() {
    aud_cache_set_budget(before);
    waitTasks();
  }
};

// ............................................................................
// Signals

inline float peak(const std::vector<float>& samples, size_t from, size_t to) {
  float result = 0;
  for (size_t i = from; i < to && i < samples.size(); ++i) {
    result = std::max(result, std::fabs(samples[i]));
  }
  return result;
}

inline uint32_t risingCrossings(const std::vector<float>& samples, size_t from, size_t to) {
  uint32_t count = 0;
  for (size_t i = from + 1; i < to && i < samples.size(); ++i) {
    if (samples[i - 1] < 0 && samples[i] >= 0) count += 1;
  }
  return count;
}

// The frequency of a sine between two frames, from its rising crossings.
inline double frequencyOf(const std::vector<float>& samples, size_t from, size_t to,
                          double rate = kRate) {
  size_t first = 0;
  size_t last = 0;
  uint32_t count = 0;
  for (size_t i = from + 1; i < to && i < samples.size(); ++i) {
    if (samples[i - 1] < 0 && samples[i] >= 0) {
      if (count == 0) first = i;
      last = i;
      count += 1;
    }
  }
  if (count < 2) return 0;
  return (count - 1) * rate / static_cast<double>(last - first);
}

// The largest step between neighbouring frames: a click is a step far above
// what the slope of the signal allows.
inline float largestStep(const std::vector<float>& samples, size_t from, size_t to) {
  float result = 0;
  for (size_t i = std::max<size_t>(from, 1); i < to && i < samples.size(); ++i) {
    result = std::max(result, std::fabs(samples[i] - samples[i - 1]));
  }
  return result;
}

// ............................................................................
// Messages

struct Reply {
  int32_t node = 0;
  std::string path;
  std::string tags;
  std::vector<int64_t> ints;
  std::vector<double> numbers;
  std::vector<std::string> texts;
  std::vector<std::vector<uint8_t>> blobs;
};

inline Reply unpack(const AudGraphMessage& record) {
  Reply reply;
  reply.node = record.node;
  const uint8_t* data = record.data;
  size_t at = 0;
  reply.path = reinterpret_cast<const char*>(data);
  at += reply.path.size() + 1;
  for (uint32_t i = 0; i < record.num_args; ++i) {
    const char tag = static_cast<char>(data[at++]);
    reply.tags += tag;
    switch (tag) {
      case 'i': {
        int32_t v;
        std::memcpy(&v, data + at, 4);
        reply.ints.push_back(v);
        at += 4;
        break;
      }
      case 'h': {
        int64_t v;
        std::memcpy(&v, data + at, 8);
        reply.ints.push_back(v);
        at += 8;
        break;
      }
      case 'f': {
        float v;
        std::memcpy(&v, data + at, 4);
        reply.numbers.push_back(v);
        at += 4;
        break;
      }
      case 'd': {
        double v;
        std::memcpy(&v, data + at, 8);
        reply.numbers.push_back(v);
        at += 8;
        break;
      }
      case 's': {
        std::string v = reinterpret_cast<const char*>(data + at);
        at += v.size() + 1;
        reply.texts.push_back(v);
        break;
      }
      case 'b': {
        uint32_t size;
        std::memcpy(&size, data + at, 4);
        at += 4;
        reply.blobs.emplace_back(data + at, data + at + size);
        at += size;
        break;
      }
      default:
        break;
    }
  }
  return reply;
}

inline std::vector<Reply> replies(Fixture& f) {
  std::vector<AudGraphMessage> records(64);
  const int32_t count = aud_graph_take_messages(f.graph, records.data(), 64);
  std::vector<Reply> result;
  for (int32_t i = 0; i < count; ++i) result.push_back(unpack(records[i]));
  return result;
}

// Sends a message with float arguments; the next block delivers it.
inline int32_t message(Fixture& f, int32_t node, const char* path,
                       std::vector<float> values = {}) {
  std::vector<AudMessageArg> args(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    args[i] = AudMessageArg{};
    args[i].struct_size = sizeof(AudMessageArg);
    args[i].type = AUD_ARG_FLOAT;
    args[i].value.f = values[i];
  }
  const AudMessage m{sizeof(AudMessage), static_cast<uint32_t>(args.size()), path,
                     args.data()};
  return aud_graph_send_message(f.graph, node, &m, nullptr, 0);
}

// Asks a node with a message without arguments and returns its answer.
inline Reply ask(Fixture& f, int32_t node, const char* path) {
  AUD_CHECK(message(f, node, path) == AUD_OK);
  f.renderOffline(256);
  const std::vector<Reply> answers = replies(f);
  AUD_CHECK(answers.size() == 1);
  return answers.empty() ? Reply{} : answers.front();
}

}  // namespace aud_sampler_test

#endif  // AUD_SAMPLER_FIXTURE_HPP

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of the sampler node on the graph engine (ticket 25):
// registration, loads with their ids, the swap of instruments, the
// controllers, the messages, the streams and the store with the budget.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "aud_directory_backend.hpp"
#include "aud_sampler_fixture.hpp"

using namespace aud_sampler_test;

namespace {

// The level of a sine of 0.5 at velocity 100: (100/127)^2 for the velocity,
// the same again for the MIDI volume sfizz starts with (CC 7 at 100 on
// curve 4) and -7.35 dB of sfizz's master volume. Centred, a mono sample
// keeps its level on both channels.
constexpr float kLevel = 0.0826f;

// An instrument of one sine sample that sounds at its own frequency on
// note 69.
std::string sineInstrument(TempFolder& folder, const std::string& name,
                           double frequency = 440, double seconds = 1.0,
                           const std::string& opcodes = "") {
  writeWav(folder.file(name + ".wav"), sine(frequency, seconds));
  return "<region> sample=" + name + ".wav pitch_keycenter=69 " + opcodes + "\n";
}

// A sine that loops for as long as the note is held: its period divides
// the sample, so the loop has no seam.
std::string loopInstrument(TempFolder& folder, const std::string& name,
                           double frequency, const std::string& opcodes = "") {
  const uint32_t frames = 48000;
  writeWav(folder.file(name + ".wav"), sine(frequency, frames / kRate));
  return "<region> sample=" + name + ".wav pitch_keycenter=69 " +
         "loop_mode=loop_continuous loop_start=0 loop_end=" +
         std::to_string(frames - 1) + " " + opcodes + "\n";
}

// The index of a frame of the last render: `from` frames after its start.
size_t since(const Fixture& f, uint32_t rendered, uint32_t from = 0) {
  return f.left.size() - rendered + from;
}

// A Scala scale of twelve equal steps of 110 cents: A4 stays, A#4 rises by
// 110 cents instead of 100.
const char* const kWideScale =
    "! wide.scl\nTwelve steps of 110 cents\n12\n110.\n220.\n330.\n440.\n550.\n660.\n"
    "770.\n880.\n990.\n1100.\n1210.\n1320.\n";

// The first number of the answer to a message without arguments, or NaN.
double numberOf(Fixture& f, int32_t node, const char* path) {
  const Reply reply = ask(f, node, path);
  return reply.numbers.empty() ? std::nan("") : reply.numbers.front();
}

// A UMP of the MIDI 2.0 channel voice protocol on channel 0.
void sendMidi2(Fixture& f, int32_t node, uint32_t status, uint32_t index, uint32_t value) {
  const uint32_t words[2] = {aud_ump_midi2_word0(0, status, 0, index, 0), value};
  f.send(node, aud_event_ump(words, 2, 0, 0));
}

// A numeric control with an argument of the type `tag`.
void sendControl(Fixture& f, int32_t node, uint32_t control, uint32_t tag, uint32_t bits = 0) {
  const AudEvent event = {sizeof(AudEvent), AUD_EVENT_CONTROL, 0, 0, 0, {control, tag, bits, 0}};
  f.send(node, event);
}

// A message with the arguments given; the next block delivers it.
int32_t sendMessage(Fixture& f, int32_t node, const char* path, std::vector<AudMessageArg> args) {
  for (AudMessageArg& arg : args) arg.struct_size = sizeof(AudMessageArg);
  const AudMessage message{sizeof(AudMessage), static_cast<uint32_t>(args.size()), path,
                           args.data()};
  return aud_graph_send_message(f.graph, node, &message, nullptr, 0);
}

// A transport request of the graph, at once.
void requestTransport(Fixture& f, uint32_t type, double value = 0) {
  constexpr uint32_t kQuarters = 4;
  AudTransportRequest request{};
  request.struct_size = sizeof(AudTransportRequest);
  request.type = type;
  request.at.struct_size = sizeof(AudTimestamp);
  request.value = value;
  request.numerator = kQuarters;
  request.denominator = kQuarters;
  AUD_CHECK(aud_graph_transport(f.graph, &request) == AUD_OK);
}

// The cycles of an amplitude modulation between two frames: the peaks of
// windows of 10 ms trace the envelope, which crosses its mean upwards once
// per cycle.
uint32_t modulationCycles(const std::vector<float>& samples, size_t from, size_t to) {
  constexpr size_t kWindow = 480;
  std::vector<float> envelope;
  for (size_t start = from; start + kWindow <= to; start += kWindow) {
    envelope.push_back(peak(samples, start, start + kWindow));
  }
  double mean = 0;
  for (float value : envelope) mean += value;
  mean /= static_cast<double>(std::max<size_t>(1, envelope.size()));
  uint32_t cycles = 0;
  for (size_t i = 1; i < envelope.size(); ++i) {
    if (envelope[i - 1] < mean && envelope[i] >= mean) cycles += 1;
  }
  return cycles;
}

// What a host logged last.
std::string g_logged;

void logToTest(void*, int32_t, const char* message) { g_logged = message; }

int32_t refuseCache(void*, const AudCacheVTable*, void*) { return AUD_ERROR_CAPACITY; }

}  // namespace

// ############################################################################
// Registration

AUD_TEST(registers_the_sampler_with_abi_0_4) {
  Fixture f;
  const AudNodeDescriptor* d =
      aud_graph_node_type_by_id(f.graph, AUD_DSP_SAMPLER_SFZ_TYPE_ID);
  AUD_CHECK(d != nullptr);
  if (d == nullptr) return;
  AUD_CHECK(d->abi_major == 0 && d->abi_minor == 4);
  AUD_CHECK(d->capabilities & AUD_NODE_CAP_MESSAGES);
  AUD_CHECK(d->capabilities & AUD_NODE_CAP_STRINGS);
  AUD_CHECK(d->capabilities & AUD_NODE_CAP_TAIL);
  AUD_CHECK(d->num_params == AUD_SFZ_NUM_PARAMS);
  AUD_CHECK(std::string(d->params[AUD_SFZ_PARAM_POLYPHONY].id) == "polyphony");
  AUD_CHECK(d->num_string_keys == 5);
  AUD_CHECK(std::string(d->string_keys[AUD_SFZ_KEY_TEXT].id) == "sfz_text");
  AUD_CHECK(d->num_output_buses == 1 && d->output_buses[0].default_channels == 2);
  // A second registration is refused like any duplicate type.
  AUD_CHECK(aud_dsp_sampler_register(f.host()) == AUD_ERROR_DUPLICATE_TYPE);
  AUD_CHECK(aud_dsp_sampler_register(nullptr) == AUD_ERROR_INVALID_ARGUMENT);
  AudHostApi other = *f.host();
  other.abi_major = 1;
  AUD_CHECK(aud_dsp_sampler_register(&other) == AUD_ERROR_ABI_MAJOR);
  other = *f.host();
  other.struct_size = 64;
  AUD_CHECK(aud_dsp_sampler_register(&other) == AUD_ERROR_ABI_MINOR);
}

AUD_TEST(refuses_a_host_without_a_function_it_calls) {
  Fixture f;
  const AudNodeDescriptor* d = aud_graph_node_type_by_id(f.graph, AUD_DSP_SAMPLER_SFZ_TYPE_ID);
  AUD_CHECK(d != nullptr);
  if (d == nullptr) return;
  // Each function the node calls, missing in turn: the node refuses the
  // host at once instead of crashing later, some on the audio thread.
  const std::vector<std::function<void(AudHostApi&)>> drops = {
      [](AudHostApi& h) { h.post_task = nullptr; },
      [](AudHostApi& h) { h.begin_load = nullptr; },
      [](AudHostApi& h) { h.load_progress = nullptr; },
      [](AudHostApi& h) { h.end_load = nullptr; },
      [](AudHostApi& h) { h.load_cancelled = nullptr; },
      [](AudHostApi& h) { h.register_cache = nullptr; },
      [](AudHostApi& h) { h.cache_changed = nullptr; },
      [](AudHostApi& h) { h.cache_budget = nullptr; },
      [](AudHostApi& h) { h.report = nullptr; },
      [](AudHostApi& h) { h.emit_message = nullptr; },
      [](AudHostApi& h) { h.struct_size = 64; },
  };
  for (const auto& drop : drops) {
    AudHostApi host = *f.host();
    drop(host);
    AUD_CHECK(d->vtable->create(d, &host) == nullptr);
  }
  AUD_CHECK(d->vtable->create(d, nullptr) == nullptr);
  // The engine's own host has them all.
  void* node = d->vtable->create(d, f.host());
  AUD_CHECK(node != nullptr);
  if (node != nullptr) d->vtable->destroy(node);
}

AUD_TEST(refuses_a_host_whose_engine_takes_no_more_caches) {
  Fixture f;
  const AudNodeDescriptor* d = aud_graph_node_type_by_id(f.graph, AUD_DSP_SAMPLER_SFZ_TYPE_ID);
  AUD_CHECK(d != nullptr);
  if (d == nullptr) return;
  AudHostApi host = *f.host();
  host.register_cache = refuseCache;
  host.log = logToTest;
  g_logged.clear();
  AUD_CHECK(d->vtable->create(d, &host) == nullptr);
  AUD_CHECK(g_logged ==
            "aud.sampler.sfz: the sample store cannot join the sample cache budget (" +
                std::to_string(AUD_ERROR_CAPACITY) + ")");
  // Without a log the node is refused all the same.
  host.log = nullptr;
  AUD_CHECK(d->vtable->create(d, &host) == nullptr);
}

// ############################################################################
// Loads

AUD_TEST(loads_an_sfz_text_and_plays_a_note) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const int32_t id = f.loadText(node, folder.file("instrument.sfz"),
                                sineInstrument(folder, "a"));
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  const AudLoadInfo info = f.load(id);
  AUD_CHECK(info.result == AUD_OK && info.node == node && info.progress == 1.0f);
  AUD_CHECK(std::string(info.name) == folder.file("instrument.sfz"));
  AUD_CHECK(info.end_ns >= info.start_ns);
  f.renderOffline(256);  // adopts the synth
  // The load began and ended in the notifications.
  const auto loads = f.notificationsOf(AUD_NOTIFY_LOAD, id);
  AUD_CHECK(loads.size() >= 2);
  if (loads.size() >= 2) {
    AUD_CHECK(loads.front().code == AUD_PENDING && loads.front().node == node);
    AUD_CHECK(loads.back().code == AUD_OK && loads.back().number == 1.0);
  }
  f.noteOn(node, 69);
  f.renderOffline(4800);
  // 440 Hz over the 4320 frames after the first 10 ms: 39.6 periods.
  const uint32_t crossings = risingCrossings(f.left, 256 + 480, 256 + 4800);
  AUD_CHECK(crossings >= 38 && crossings <= 40);
  AUD_CHECK_NEAR(peak(f.left, 256 + 480, 256 + 4800), kLevel, 0.002);
  f.noteOff(node, 69);
  f.renderOffline(48000);
  AUD_CHECK(peak(f.left, f.left.size() - 4800, f.left.size()) < 1e-4f);
}

AUD_TEST(loads_an_sfz_file_and_finds_its_samples_beside_it) {
  Fixture f;
  TempFolder folder;
  folder.createFolder("instrument/samples");
  writeWav(folder.file("instrument/samples/a.wav"), sine(440, 1.0));
  writeText(folder.file("instrument/piano.sfz"),
            "<control> default_path=samples/\n<region> sample=a.wav pitch_keycenter=69\n");
  const int32_t node = f.sampler();
  const int32_t id = f.loadFile(node, folder.file("instrument/piano.sfz"));
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_OK);
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, 256 + 480, 256 + 4800), 440.0, 1.0);
  AUD_CHECK_NEAR(peak(f.left, 256 + 480, 256 + 4800), kLevel, 0.002);
}

AUD_TEST(loads_an_instrument_from_apk_assets_and_plays_it) {
  Fixture f;
  TempFolder folder;
  folder.createFolder("content/sfz/presets/x");
  folder.createFolder("content/sfz/samples/piano");
  // Longer than the head: a note streams the rest through the backend.
  writeWav(folder.file("content/sfz/samples/piano/A4.wav"), sine(440, 1.0));
  writeText(folder.file("content/sfz/common.sfz"), "<control> default_path=samples/piano/\n");
  writeText(folder.file("content/sfz/presets/x/main.sfz"),
            "#include \"../../common.sfz\"\n"
            "<control> default_path=../../samples/piano/\n"
            "<region> sample=a4.wav pitch_keycenter=69\n");
  ScopedApkAssets assets(folder.path);
  const int32_t node = f.sampler();
  const int32_t id = f.loadFile(node, "apk:/content/sfz/presets/x/main.sfz");
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_OK);
  AUD_CHECK(std::string(f.load(id).message).empty());
  // The stream of the note reads the sample through the backend too.
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(24000);
  AUD_CHECK(assets.backend->openedPaths.back() == "content/sfz/samples/piano/A4.wav");
  AUD_CHECK(assets.backend->opens == 5);
  AUD_CHECK_NEAR(frequencyOf(f.left, 256 + 12000, 256 + 24000), 440.0, 1.0);
  AUD_CHECK_NEAR(peak(f.left, 256 + 12000, 256 + 24000), kLevel, 0.002);
  AUD_CHECK(f.diagnostics(AUD_ERROR_UNDERFLOW, node) == 0);
  // A Scala file from the assets too.
  writeText(folder.file("content/sfz/wide.scl"), kWideScale);
  const int32_t scala = aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_SCALA_FILE,
                                             "apk:/content/sfz/wide.scl");
  AUD_CHECK(scala > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(scala).result == AUD_OK);
}

AUD_TEST(fails_an_apk_load_without_the_assets_of_an_android_app) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  const int32_t id = f.loadFile(node, "apk:/content/sfz/presets/x/main.sfz");
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_ERROR_UNSUPPORTED);
  AUD_CHECK(std::string(f.load(id).message) ==
            "apk:/ paths read the assets of an Android app, which this platform has none of: "
            "apk:/content/sfz/presets/x/main.sfz");
  // A text whose samples would live in the assets, and a Scala file there.
  const int32_t text = f.loadText(node, "apk:/content/b.sfz", "<region> sample=b.wav\n");
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(text).result == AUD_ERROR_UNSUPPORTED);
  AUD_CHECK(std::string(f.load(text).message).find("apk:/content/b.sfz") != std::string::npos);
  AUD_CHECK(aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_VIRTUAL_PATH,
                                 folder.file("c.sfz").c_str()) == AUD_OK);
  const int32_t scala = aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_SCALA_FILE,
                                             "apk:/content/wide.scl");
  AUD_CHECK(scala > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(scala).result == AUD_ERROR_UNSUPPORTED);
  // The old instrument plays on.
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 375.0, 1.0);
}

AUD_TEST(ends_a_failed_load_and_keeps_playing_the_instrument) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  // A file that does not exist.
  const int32_t missing = f.loadFile(node, folder.file("missing.sfz"));
  AUD_CHECK(missing > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  const AudLoadInfo info = f.load(missing);
  AUD_CHECK(info.result == AUD_ERROR_FAILED);
  AUD_CHECK(std::string(info.message).find("sfizz could not load " + folder.file("missing.sfz") +
                                           ": missing.sfz at line 1: ") == 0);
  // A text whose only sample does not exist leaves no region.
  const int32_t empty = f.loadText(node, folder.file("b.sfz"), "<region> sample=nothing.wav\n");
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(empty).result == AUD_ERROR_FAILED);
  AUD_CHECK(std::string(f.load(empty).message) == "sfizz could not read 1 sample: nothing.wav");
  // The note of the old instrument plays on, without a fade.
  f.renderOffline(4800);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800), f.left.size()), kLevel, 0.002);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800), f.left.size()), 375.0, 1.0);
  const auto ends = f.notificationsOf(AUD_NOTIFY_LOAD, missing);
  AUD_CHECK(!ends.empty() && ends.back().code == AUD_ERROR_FAILED);
  // Some samples missing and an effect sfizz does not know: the load ends
  // well and names them; the effect passes the signal through.
  const int32_t partial = f.loadText(
      node, folder.file("c.sfz"),
      loopInstrument(folder, "c", 750) + "<region> sample=gone.wav key=80\n" +
          "<effect> type=fverb\n");
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(partial).result == AUD_OK);
  AUD_CHECK(std::string(f.load(partial).message) ==
            "sfizz could not read 1 sample: gone.wav; sfizz does not know 1 effect type: fverb");
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 750.0, 1.0);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800, 480), f.left.size()), kLevel, 0.002);
}

AUD_TEST(a_newer_load_supersedes_one_that_waits) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const std::string a = loopInstrument(folder, "a", 375);
  const std::string b = loopInstrument(folder, "b", 750);
  int32_t first = 0;
  int32_t second = 0;
  {
    WorkerHold hold(f.host());
    first = f.loadText(node, folder.file("a.sfz"), a);
    second = f.loadText(node, folder.file("b.sfz"), b);
    AUD_CHECK(first > 0 && second > first);
  }
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(first).result == AUD_ERROR_CANCELLED);
  AUD_CHECK(f.load(second).result == AUD_OK);
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 750.0, 1.0);
}

AUD_TEST(cancels_a_load_and_keeps_the_instrument) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(2048);
  int32_t id = 0;
  {
    WorkerHold hold(f.host());
    id = f.loadText(node, folder.file("b.sfz"), loopInstrument(folder, "b", 750));
    AUD_CHECK(id > 0);
    AUD_CHECK(aud_graph_cancel_load(f.graph, id) == AUD_OK);
  }
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  const AudLoadInfo info = f.load(id);
  AUD_CHECK(info.result == AUD_ERROR_CANCELLED && info.cancelled == 1);
  // A load that ended cannot be cancelled.
  AUD_CHECK(aud_graph_cancel_load(f.graph, id) == AUD_ERROR_NOT_FOUND);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800), f.left.size()), 375.0, 1.0);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800), f.left.size()), kLevel, 0.002);
}

AUD_TEST(unloads_the_instrument_with_an_empty_text) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // A text with only a comment, a file with only a comment (ticket 27) and
  // an empty text: each loads an instrument without regions, which
  // silences the node.
  writeText(folder.file("silent.sfz"), "// silent\n");
  for (int kind = 0; kind < 3; ++kind) {
    AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
    f.renderOffline(256);
    f.noteOn(node, 69);
    f.renderOffline(2048);
    AUD_CHECK(peak(f.left, since(f, 2048, 256), f.left.size()) > 0.5f * kLevel);
    const int32_t id = kind == 0   ? f.loadText(node, folder.file("a.sfz"), "// silent")
                       : kind == 1 ? f.loadFile(node, folder.file("silent.sfz"))
                                   : f.loadText(node, folder.file("a.sfz"), "");
    AUD_CHECK(id > 0);
    AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
    AUD_CHECK(f.load(id).result == AUD_OK);
    f.renderOffline(2048);
    AUD_CHECK(peak(f.left, since(f, 2048, 256), f.left.size()) == 0.0f);
    const Reply regions = ask(f, node, "/num_regions");
    AUD_CHECK(regions.path == "/num_regions" && regions.ints.size() == 1 &&
              regions.ints.front() == 0);
  }
  // A Scala tuning without an instrument starts no load.
  AUD_CHECK(aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_SCALA_TEXT, "") == AUD_OK);
  AUD_CHECK(aud_graph_set_string(f.graph, node, 99, "x") == AUD_ERROR_INVALID_ARGUMENT);
}

AUD_TEST(a_load_that_starts_with_a_reload_ends_well) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const std::string a = loopInstrument(folder, "a", 375);
  const std::string b = loopInstrument(folder, "b", 750);
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), a));
  // A change of the polyphony reloads on a worker while the control thread
  // starts a load: whichever comes first, the load is the newer one, and a
  // load yields only to a newer load.
  constexpr int kRounds = 40;
  std::vector<int32_t> ids;
  for (int i = 0; i < kRounds; ++i) {
    f.param(node, AUD_SFZ_PARAM_POLYPHONY, static_cast<float>(16 + i % 2));
    f.renderLive(256);
    const bool high = i % 2 == 0;
    ids.push_back(f.loadText(node, folder.file(high ? "b.sfz" : "a.sfz"), high ? b : a));
    f.renderLive(256);
  }
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  for (int32_t id : ids) {
    const int32_t result = f.load(id).result;
    AUD_CHECK(result == AUD_OK || result == AUD_ERROR_CANCELLED);
  }
  AUD_CHECK(f.load(ids.back()).result == AUD_OK);
  waitTasks();
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 375.0, 1.0);
}

AUD_TEST(a_load_that_runs_takes_a_polyphony_change) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  int32_t id = 0;
  {
    // The load and the reload of the change wait behind the hold; the
    // reload stands back while the load runs, which takes the change.
    WorkerHold hold(f.host());
    id = f.loadText(node, folder.file("b.sfz"), loopInstrument(folder, "b", 750));
    AUD_CHECK(id > 0);
    f.param(node, AUD_SFZ_PARAM_POLYPHONY, 2);
    f.renderLive(256);
  }
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  waitTasks();
  AUD_CHECK(f.load(id).result == AUD_OK);
  f.renderOffline(512);
  f.noteOn(node, 60);
  f.noteOn(node, 64);
  f.noteOn(node, 67);
  f.renderOffline(2048);
  const Reply voices = ask(f, node, "/num_active_voices");
  AUD_CHECK(voices.ints == std::vector<int64_t>{2});
  f.noteOff(node, 60);
  f.noteOff(node, 64);
  f.noteOff(node, 67);
  f.renderOffline(9600);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 750.0, 1.0);
}

// ############################################################################
// The swap of instruments

AUD_TEST(crossfades_from_one_instrument_to_the_next) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK(f.loadAndWait(node, folder.file("b.sfz"), loopInstrument(folder, "b", 750)));
  const size_t swap = f.left.size();
  f.renderOffline(2048);
  // The note of the old instrument fades out over 5 ms, 240 frames, without
  // a click: no step beyond the slope of the sine at its level.
  AUD_CHECK(peak(f.left, swap, swap + 16) > 0.5f * kLevel);
  AUD_CHECK(peak(f.left, swap + 200, swap + 240) < 0.2f * kLevel);
  AUD_CHECK(largestStep(f.left, swap - 256, swap + 256) <
            1.2f * static_cast<float>(2 * kPi * 375 / kRate) * kLevel);
  // The new instrument starts without notes.
  AUD_CHECK(peak(f.left, swap + 240, f.left.size()) == 0.0f);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 750.0, 1.0);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800, 480), f.left.size()), kLevel, 0.002);
}

AUD_TEST(a_new_instrument_takes_over_the_controllers) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.cc(node, 7, 0);     // the MIDI volume closed
  f.bend(node, 16383);  // two semitones up, sfizz's default range
  f.renderOffline(256);
  AUD_CHECK(f.loadAndWait(node, folder.file("b.sfz"), loopInstrument(folder, "b", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK(peak(f.left, since(f, 4800), f.left.size()) == 0.0f);
  f.cc(node, 7, 127);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 4800), f.left.size()),
                 375.0 * std::pow(2.0, 2.0 / 12.0), 1.5);
  AUD_CHECK(peak(f.left, since(f, 9600, 4800), f.left.size()) > kLevel);
}

AUD_TEST(a_new_instrument_starts_no_region_of_a_restored_controller) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.cc(node, 64, 127);
  f.renderOffline(256);
  // B plays a sample when the pedal goes down.
  writeWav(folder.file("pedal.wav"), sine(750, 1.0));
  const std::string b = loopInstrument(folder, "b", 375) +
                        "<region> sample=pedal.wav on_locc64=64 on_hicc64=127\n";
  AUD_CHECK(f.loadAndWait(node, folder.file("b.sfz"), b));
  f.renderOffline(4800);
  AUD_CHECK(peak(f.left, since(f, 4800), f.left.size()) == 0.0f);
  const Reply pedal = ask(f, node, "/cc64/value");
  AUD_CHECK(!pedal.numbers.empty() && pedal.numbers.front() == 1.0);
  // A pedal the player presses again does.
  f.cc(node, 64, 0);
  f.cc(node, 64, 127);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 750.0, 1.0);
}

AUD_TEST(a_sample_deleted_after_the_load_plays_its_head) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("gone.sfz"),
                          sineInstrument(folder, "gone", 375, 2.0)));
  std::remove(folder.file("gone.wav").c_str());
  f.renderLive(256);
  const size_t start = f.left.size();
  f.noteOn(node, 69);
  f.renderLive(kHeadFrames + 4096);
  waitTasks();
  f.renderLive(4096);
  // The head sounds, then silence; the voice keeps its time and ends with
  // the sample, and the stream is not asked for again and again.
  AUD_CHECK_NEAR(peak(f.left, start + 480, start + 8000), kLevel, 0.002);
  AUD_CHECK(peak(f.left, start + kHeadFrames, f.left.size()) == 0.0f);
  AUD_CHECK(f.diagnostics(AUD_ERROR_UNDERFLOW, node) > 0);
  AUD_CHECK(cacheUsage().tasks == 0);
}

#if AUD_TEST_ALLOCATION_LIMIT
AUD_TEST(drops_a_sample_whose_head_does_not_fit_into_memory) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // The header claims 4 GB of frames; the offset asks for a head of 300
  // million of them, more than memory holds.
  writeClaimingWav(folder.file("claims.wav"), sine(375, 0.5), 0xFFFFFFF0u);
  const int32_t id =
      f.loadText(node, folder.file("claims.sfz"),
                 "<region> sample=claims.wav key=60 offset=300000000\n" +
                     sineInstrument(folder, "kept", 375, 1.0, "key=69"));
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_OK);
  AUD_CHECK(std::string(f.load(id).message) == "sfizz could not read 1 sample: claims.wav");
  // A note on the key of the dropped region stays silent, the region that
  // is left sounds.
  f.renderOffline(256);
  f.noteOn(node, 60);
  f.renderOffline(4800);
  AUD_CHECK(peak(f.left, since(f, 4800), f.left.size()) == 0.0f);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800, 480), f.left.size()), kLevel, 0.002);
}
#endif

AUD_TEST(swaps_instruments_while_it_renders_live) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const std::string a = loopInstrument(folder, "a", 375);
  const std::string b = loopInstrument(folder, "b", 750);
  std::vector<int32_t> ids;
  for (int i = 0; i < 12; ++i) {
    ids.push_back(f.loadText(node, folder.file(i % 2 == 0 ? "a.sfz" : "b.sfz"),
                             i % 2 == 0 ? a : b));
    f.noteOn(node, static_cast<uint32_t>(60 + i));
    f.renderLive(1024);
    f.cc(node, 1, static_cast<uint32_t>(i * 10));
    f.renderLive(512);
    f.noteOff(node, static_cast<uint32_t>(60 + i));
  }
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  for (int32_t id : ids) {
    const int32_t result = f.load(id).result;
    AUD_CHECK(result == AUD_OK || result == AUD_ERROR_CANCELLED);
  }
  AUD_CHECK(f.load(ids.back()).result == AUD_OK);
  f.renderLive(1024);
  f.noteOn(node, 69);
  f.renderLive(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800, 480), f.left.size()), 750.0, 1.0);
}

// ############################################################################
// Parameters

AUD_TEST(ramps_the_volume) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(2048);
  f.param(node, AUD_SFZ_PARAM_VOLUME, -6.0206f);
  f.renderOffline(2048);
  AUD_CHECK_NEAR(peak(f.left, since(f, 2048, 256), f.left.size()), 0.5f * kLevel, 0.002);
  // A ramp of 4800 frames back to 0 dB.
  f.param(node, AUD_SFZ_PARAM_VOLUME, 0.0f, 4800);
  f.renderOffline(9600);
  AUD_CHECK(peak(f.left, since(f, 9600), since(f, 9600, 512)) < 0.6f * kLevel);
  AUD_CHECK_NEAR(peak(f.left, since(f, 9600, 4800), f.left.size()), kLevel, 0.002);
  // Out of range: clamped to -60 dB.
  f.param(node, AUD_SFZ_PARAM_VOLUME, -1000.0f);
  f.renderOffline(2048);
  AUD_CHECK(peak(f.left, since(f, 2048, 256), f.left.size()) < 0.002f * kLevel);
}

AUD_TEST(holds_the_polyphony) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.param(node, AUD_SFZ_PARAM_POLYPHONY, 2);
  // The block applies the parameter and asks for a synth with two voices;
  // the offline render waits for it, the next block adopts it.
  f.renderOffline(512);
  f.noteOn(node, 60);
  f.noteOn(node, 64);
  f.noteOn(node, 67);
  f.renderOffline(2048);
  Reply voices = ask(f, node, "/num_active_voices");
  AUD_CHECK(voices.ints.size() == 1 && voices.ints.front() == 2);
  f.param(node, AUD_SFZ_PARAM_POLYPHONY, 64);
  f.renderOffline(512);
  f.noteOn(node, 60);
  f.noteOn(node, 64);
  f.noteOn(node, 67);
  f.renderOffline(2048);
  voices = ask(f, node, "/num_active_voices");
  AUD_CHECK(voices.ints.size() == 1 && voices.ints.front() == 3);
}

AUD_TEST(tunes_with_the_tuning_frequency_and_a_scala_text) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.param(node, AUD_SFZ_PARAM_TUNING_FREQUENCY, 432.0f);
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 480), f.left.size()), 375.0 * 432 / 440,
                 1.0);
  f.noteOff(node, 69);
  f.param(node, AUD_SFZ_PARAM_TUNING_FREQUENCY, 440.0f);
  const int32_t id = aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_SCALA_TEXT, kWideScale);
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_OK);
  f.renderOffline(2048);
  f.noteOn(node, 70);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 480), f.left.size()),
                 375.0 * std::pow(2.0, 1.1 / 12.0), 1.5);
}

AUD_TEST(ramps_the_volume_before_the_first_instrument) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // A ramp reaches the node as an event of a block, while no instrument
  // plays yet.
  f.param(node, AUD_SFZ_PARAM_VOLUME, -6.0206f, 480);
  f.renderOffline(1024);
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800, 480), f.left.size()), 0.5f * kLevel, 0.002);
}

AUD_TEST(rounds_the_quality_steps) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  // Automation may land next to a step.
  f.param(node, AUD_SFZ_PARAM_SAMPLE_QUALITY, 2.9999998f);
  f.param(node, AUD_SFZ_PARAM_OSCILLATOR_QUALITY, 1.9999999f);
  f.renderOffline(256);
  AUD_CHECK(ask(f, node, "/sample_quality").ints == std::vector<int64_t>{3});
  AUD_CHECK(ask(f, node, "/oscillator_quality").ints == std::vector<int64_t>{2});
}

AUD_TEST(stretches_the_tuning) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  // Three octaves above A4, where a piano's stretch is wide.
  constexpr uint32_t kHighNote = 105;
  const auto frequencyOfHighNote = [&f, node] {
    f.renderOffline(2048);
    f.noteOn(node, kHighNote);
    f.renderOffline(9600);
    const double frequency = frequencyOf(f.left, since(f, 9600, 480), f.left.size());
    f.noteOff(node, kHighNote);
    f.renderOffline(9600);
    return frequency;
  };
  const double equal = frequencyOfHighNote();
  AUD_CHECK_NEAR(equal, 375.0 * 8, 2.0);
  f.param(node, AUD_SFZ_PARAM_STRETCH_TUNING, 1.0f);
  const double stretched = frequencyOfHighNote();
  AUD_CHECK(stretched > equal * std::pow(2.0, 5.0 / 1200.0));
}

AUD_TEST(tunes_with_a_scala_file) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // Without an instrument the tuning waits for one.
  writeText(folder.file("wide.scl"), kWideScale);
  AUD_CHECK(aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_SCALA_FILE,
                                 folder.file("wide.scl").c_str()) == AUD_OK);
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 70);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 480), f.left.size()),
                 375.0 * std::pow(2.0, 1.1 / 12.0), 1.5);
  // A file that is not there fails the load and keeps the instrument.
  const int32_t id = aud_graph_set_string(f.graph, node, AUD_SFZ_KEY_SCALA_FILE,
                                          folder.file("missing.scl").c_str());
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_ERROR_FAILED);
  AUD_CHECK(std::string(f.load(id).message) ==
            "sfizz could not load " + folder.file("missing.scl"));
}

// ############################################################################
// Events

AUD_TEST(plays_midi_2_and_reports_per_note_controllers_it_cannot_play) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  uint32_t words[2];
  aud_ump_midi2_note(0, 0, 1, 69, 0xFFFF, 0, 0, words);
  f.send(node, aud_event_ump(words, 2, 0, 0));
  aud_ump_midi2_per_note_controller(0, 0, 69, 7, 0x80000000u, words);
  f.send(node, aud_event_ump(words, 2, 0, 0));
  f.renderOffline(4800);
  // Velocity 1.0 is (127/100)^2 louder than velocity 100.
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800, 480), f.left.size()),
                 kLevel * (127.0f / 100) * (127.0f / 100), 0.003);
  AUD_CHECK(f.diagnostics(AUD_ERROR_UNSUPPORTED, node) == 1);
  // A numeric control names a CC of sfizz: CC 7 at 0 closes the volume,
  // smoothed over a few ms.
  f.send(node, aud_event_control_float(7, 0.0f, 0, 0));
  f.renderOffline(4800);
  AUD_CHECK(peak(f.left, since(f, 4800, 2400), f.left.size()) < 1e-4f);
}

AUD_TEST(plays_the_controllers_of_midi_1_and_2) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  // MIDI 2.0 with 32-bit values: a CC at a quarter, the pitch bend a half
  // up, the channel pressure at three quarters, poly pressure on note 61.
  sendMidi2(f, node, AUD_UMP_STATUS_CONTROL_CHANGE, 1, 0x40000000u);
  sendMidi2(f, node, AUD_UMP_STATUS_PITCH_BEND, 0, 0xC0000000u);
  sendMidi2(f, node, AUD_UMP_STATUS_CHANNEL_PRESSURE, 0, 0xC0000000u);
  sendMidi2(f, node, AUD_UMP_STATUS_POLY_PRESSURE, 61, 0x80000000u);
  // MIDI 1.0 with 7-bit values: poly pressure on note 60.
  const uint32_t poly = aud_ump_midi1_word(0, 0xA0, 60, 127);
  f.send(node, aud_event_ump(&poly, 1, 0, 0));
  f.renderOffline(256);
  AUD_CHECK_NEAR(numberOf(f, node, "/cc1/value"), 0.25, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/pitch_bend"), 0.5, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/aftertouch"), 0.75, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/poly_aftertouch/61"), 0.5, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/poly_aftertouch/60"), 1.0, 1e-6);
  // MIDI 1.0 channel pressure and pitch bend.
  const uint32_t pressure = aud_ump_midi1_word(0, 0xD0, 32, 0);
  f.send(node, aud_event_ump(&pressure, 1, 0, 0));
  f.bend(node, 4096);
  f.renderOffline(256);
  AUD_CHECK_NEAR(numberOf(f, node, "/aftertouch"), 32.0 / 127, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/pitch_bend"), -0.5, 1e-6);
}

AUD_TEST(changes_the_program_with_midi_1_and_2) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const std::string text = loopInstrument(folder, "low", 375, "loprog=0 hiprog=4") +
                           loopInstrument(folder, "high", 750, "loprog=5 hiprog=9");
  AUD_CHECK(f.loadAndWait(node, folder.file("programs.sfz"), text));
  f.renderOffline(256);
  const auto frequencyOfNote = [&f, node] {
    f.noteOn(node, 69);
    f.renderOffline(4800);
    const double frequency = frequencyOf(f.left, since(f, 4800, 480), f.left.size());
    f.noteOff(node, 69);
    f.renderOffline(9600);
    return frequency;
  };
  AUD_CHECK_NEAR(frequencyOfNote(), 375.0, 1.0);
  const uint32_t program = aud_ump_midi1_word(0, 0xC0, 5, 0);
  f.send(node, aud_event_ump(&program, 1, 0, 0));
  AUD_CHECK_NEAR(frequencyOfNote(), 750.0, 1.0);
  sendMidi2(f, node, AUD_UMP_STATUS_PROGRAM_CHANGE, 0, 0);  // program 0 in the top byte
  AUD_CHECK_NEAR(frequencyOfNote(), 375.0, 1.0);
}

AUD_TEST(takes_numeric_controls_of_every_type) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  float quarter = 0.25f;
  uint32_t quarterBits;
  std::memcpy(&quarterBits, &quarter, sizeof(quarterBits));
  sendControl(f, node, 20, AUD_ARG_INT32, 64);
  sendControl(f, node, 21, AUD_ARG_TRUE);
  sendControl(f, node, 22, AUD_ARG_TRUE);
  sendControl(f, node, 22, AUD_ARG_FALSE);
  sendControl(f, node, 23, AUD_ARG_FLOAT, quarterBits);
  // Another type, and a control beyond sfizz's CCs, change nothing.
  sendControl(f, node, 24, AUD_ARG_DOUBLE);
  sendControl(f, node, 600, AUD_ARG_TRUE);
  f.renderOffline(256);
  AUD_CHECK_NEAR(numberOf(f, node, "/cc20/value"), 64.0 / 127, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/cc21/value"), 1.0, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/cc22/value"), 0.0, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/cc23/value"), 0.25, 1e-6);
  AUD_CHECK_NEAR(numberOf(f, node, "/cc24/value"), 0.0, 1e-6);
}

AUD_TEST(a_first_instrument_takes_over_the_controllers_sent_before_it) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  f.cc(node, 7, 0);     // the MIDI volume closed
  f.bend(node, 16383);  // two semitones up, sfizz's default range
  // Nothing answers a message without an instrument: it counts as
  // unsupported.
  AUD_CHECK(message(f, node, "/cc7/value", {0.5f}) == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(f.diagnostics(AUD_ERROR_UNSUPPORTED, node) == 1);
  AUD_CHECK(replies(f).empty());
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  AUD_CHECK(peak(f.left, since(f, 4800), f.left.size()) == 0.0f);
  f.cc(node, 7, 127);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 4800), f.left.size()),
                 375.0 * std::pow(2.0, 2.0 / 12.0), 1.5);
}

// ############################################################################
// Messages (osc-001)

AUD_TEST(answers_messages_through_the_engine) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const std::string text = sineInstrument(folder, "a") + sineInstrument(folder, "b", 880);
  AUD_CHECK(f.loadAndWait(node, folder.file("two.sfz"), text));
  f.renderOffline(256);
  const Reply regions = ask(f, node, "/num_regions");
  AUD_CHECK(regions.node == node && regions.path == "/num_regions");
  AUD_CHECK(regions.ints.size() == 1 && regions.ints.front() == 2);
  const Reply sample = ask(f, node, "/region1/sample");
  AUD_CHECK(sample.tags == "s" && !sample.texts.empty() && sample.texts.front() == "b.wav");
  const Reply hello = ask(f, node, "/hello");
  AUD_CHECK(hello.path == "/hello" && hello.tags == "s" && hello.texts.front().empty());
  // A message sets a controller; sfizz answers what it holds.
  AUD_CHECK(message(f, node, "/cc7/value", {0.25f}) == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(replies(f).empty());
  const Reply cc = ask(f, node, "/cc7/value");
  AUD_CHECK(cc.tags == "f" && !cc.numbers.empty() && cc.numbers.front() == 0.25);
  // A path sfizz does not know has no answer.
  AUD_CHECK(message(f, node, "/nothing") == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(replies(f).empty());
  AUD_CHECK(f.stats().messages_dropped == 0);
}

AUD_TEST(refuses_messages_sfizz_would_copy_or_parse) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), sineInstrument(folder, "a")));
  f.renderOffline(256);
  // A string, a blob and five arguments: sfizz would copy or parse them on
  // the audio thread, or no path of it takes them.
  AudMessageArg text{};
  text.type = AUD_ARG_STRING;
  text.value.s = "forward";
  const uint8_t bytes[4] = {1, 2, 3, 4};
  AudMessageArg blob{};
  blob.type = AUD_ARG_BLOB;
  blob.size = sizeof(bytes);
  blob.value.b = bytes;
  AudMessageArg number{};
  number.type = AUD_ARG_FLOAT;
  number.value.f = 0.5f;
  AUD_CHECK(sendMessage(f, node, "/region0/direction", {text}) == AUD_OK);
  AUD_CHECK(sendMessage(f, node, "/region0/sample", {blob}) == AUD_OK);
  AUD_CHECK(sendMessage(f, node, "/cc7/value", std::vector<AudMessageArg>(5, number)) == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(replies(f).empty());
  AUD_CHECK(f.diagnostics(AUD_ERROR_UNSUPPORTED, node) == 3);
  AUD_CHECK(f.stats().messages_dropped == 0);
}

// ############################################################################
// The transport

AUD_TEST(follows_the_tempo_of_the_transport) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // An LFO swings the volume of a held sine by 12 dB once per beat.
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"),
                          loopInstrument(folder, "a", 375, "lfo1_beats=1 lfo1_volume=12")));
  requestTransport(f, AUD_TRANSPORT_REQUEST_SET_TEMPO, 120);
  requestTransport(f, AUD_TRANSPORT_REQUEST_START);
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  // Two seconds at 120 and at 240 beats per minute: 4 and 8 cycles.
  constexpr uint32_t kTwoSeconds = 96000;
  f.renderOffline(kTwoSeconds);
  AUD_CHECK_NEAR(modulationCycles(f.left, since(f, kTwoSeconds), f.left.size()), 4, 1);
  requestTransport(f, AUD_TRANSPORT_REQUEST_SET_TEMPO, 240);
  f.renderOffline(4800);
  f.renderOffline(kTwoSeconds);
  AUD_CHECK_NEAR(modulationCycles(f.left, since(f, kTwoSeconds), f.left.size()), 8, 1);
}

// ############################################################################
// Streams

AUD_TEST(streams_a_long_sample_in_an_offline_render) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), sineInstrument(folder, "a", 375, 3.0)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(3 * 48000);
  // The whole sample sounds: the stream delivers before a block needs it.
  AUD_CHECK_NEAR(peak(f.left, since(f, 144000, 136000), since(f, 144000, 140000)), kLevel,
                 0.002);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 144000, kHeadFrames), since(f, 144000, 140000)),
                 375.0, 0.5);
  AUD_CHECK(f.diagnostics(AUD_ERROR_UNDERFLOW) == 0);
  // The end of the sample ends the voice, after a release of a few ms.
  f.renderOffline(4800);
  AUD_CHECK(peak(f.left, since(f, 4800, 1024), f.left.size()) < 1e-5f);
}

AUD_TEST(plays_silence_while_a_stream_is_late_and_resumes_in_time) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // A sample of its own, so that no earlier test streamed it.
  AUD_CHECK(f.loadAndWait(node, folder.file("late.sfz"),
                          sineInstrument(folder, "late", 375, 2.0)));
  f.renderLive(256);
  const size_t start = f.left.size();
  {
    WorkerHold hold(f.host());
    f.noteOn(node, 69);
    f.renderLive(kHeadFrames + 4096);
    // The head sounds, then silence: the stream waits behind the hold.
    AUD_CHECK_NEAR(peak(f.left, start + 480, start + 8000), kLevel, 0.002);
    AUD_CHECK(peak(f.left, start + kHeadFrames, f.left.size()) == 0.0f);
    AUD_CHECK(f.diagnostics(AUD_ERROR_UNDERFLOW, node) > 0);
  }
  waitTasks();
  f.renderLive(4096);
  // The voice kept its position: it resumes where the sample is now.
  const size_t resumed = f.left.size() - 4096;
  AUD_CHECK_NEAR(peak(f.left, resumed + 256, f.left.size()), kLevel, 0.002);
  AUD_CHECK_NEAR(frequencyOf(f.left, resumed + 256, f.left.size()), 375.0, 1.0);
  // It ends with the sample, 2 s after the note.
  const size_t end = start + 2 * 48000;
  if (f.left.size() < end + 4096) {
    f.renderLive(static_cast<uint32_t>(end + 4096 - f.left.size()));
  }
  AUD_CHECK(peak(f.left, end + 1024, f.left.size()) < 1e-5f);
}

// ############################################################################
// The store and the budget

AUD_TEST(shares_samples_between_nodes_and_graphs) {
  TempFolder folder;
  const std::string text = sineInstrument(folder, "shared", 375, 2.0);
  Fixture f;
  const int32_t first = f.sampler();
  waitTasks();
  const int64_t bytesBefore = aud_dsp_sampler_store_bytes();
  const int32_t filesBefore = aud_dsp_sampler_store_files();
  const int64_t usageBefore = cacheUsage().bytes;
  AUD_CHECK(f.loadAndWait(first, folder.file("a.sfz"), text));
  // The head of the mono sample: 8192 frames of 4 bytes.
  const int64_t head = static_cast<int64_t>(kHeadFrames) * 4;
  AUD_CHECK(aud_dsp_sampler_store_files() == filesBefore + 1);
  AUD_CHECK(aud_dsp_sampler_store_bytes() == bytesBefore + head);
  AUD_CHECK(cacheUsage().bytes == usageBefore + head);
  // A second node and a node of a second graph share the file.
  const int32_t second = f.sampler();
  AUD_CHECK(f.loadAndWait(second, folder.file("a.sfz"), text));
  Fixture other;
  const int32_t third = other.sampler();
  AUD_CHECK(other.loadAndWait(third, folder.file("a.sfz"), text));
  AUD_CHECK(aud_dsp_sampler_store_files() == filesBefore + 1);
  AUD_CHECK(aud_dsp_sampler_store_bytes() == bytesBefore + head);
  AUD_CHECK(cacheUsage().bytes == usageBefore + head);
  AUD_CHECK(cacheUsage().caches >= 1);
  // Two graphs play it; it streams once: 2 s of 4 bytes.
  f.renderOffline(256);
  other.renderOffline(256);
  f.noteOn(first, 69);
  other.noteOn(third, 69);
  f.renderOffline(4800);
  other.renderOffline(4800);
  waitTasks();
  AUD_CHECK(aud_dsp_sampler_store_bytes() == bytesBefore + head + 2 * 48000 * 4);
  AUD_CHECK_NEAR(peak(other.left, since(other, 4800, 480), other.left.size()), kLevel, 0.002);
}

AUD_TEST(trims_what_nobody_uses_to_the_budget) {
  TempFolder folder;
  Fixture f;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), sineInstrument(folder, "trim", 375, 1.0)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(48000 + 4800);  // the whole sample streamed and played
  waitTasks();
  const int64_t head = static_cast<int64_t>(kHeadFrames) * 4;
  AUD_CHECK(aud_dsp_sampler_store_bytes() >= head + 48000 * 4);
  const AudCacheUsage before = cacheUsage();
  {
    // Nothing fits: the store frees every stream no voice reads and every
    // file no pool holds. The head of the loaded instrument stays.
    BudgetScope budget(1);
    AUD_CHECK(aud_dsp_sampler_store_bytes() == head);
    AUD_CHECK(aud_dsp_sampler_store_files() == 1);
    const AudCacheUsage after = cacheUsage();
    AUD_CHECK(after.bytes == head);
    AUD_CHECK(after.trims > before.trims);
    AUD_CHECK(after.trimmed >= before.trimmed + 48000 * 4);
    // The trimmed sample streams again for the next note and plays to its
    // end; a voice that reads it keeps it.
    f.noteOn(node, 69);
    f.renderOffline(24000);
    AUD_CHECK_NEAR(peak(f.left, since(f, 24000, 16000), f.left.size()), kLevel, 0.002);
    AUD_CHECK(aud_dsp_sampler_store_bytes() == head + 48000 * 4);
    f.renderOffline(24000 + 4800);
  }
  // The node goes: its file is unheld and the next trim frees it.
  f.remove(node);
  f.renderOffline(2048);
  f.destroy();
  {
    BudgetScope budget(1);
    AUD_CHECK(aud_dsp_sampler_store_bytes() == 0);
    AUD_CHECK(aud_dsp_sampler_store_files() == 0);
  }
}

AUD_TEST(refuses_an_instrument_whose_heads_exceed_the_budget) {
  TempFolder folder;
  Fixture f;
  const int32_t node = f.sampler();
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(2048);
  {
    // Two mono samples of 1 s pin two heads of 32 KB.
    BudgetScope budget(48 * 1024);
    const int32_t id = f.loadText(node, folder.file("big.sfz"),
                                  sineInstrument(folder, "big1", 375) +
                                      sineInstrument(folder, "big2", 750, 1.0, "key=80"));
    AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
    const AudLoadInfo info = f.load(id);
    AUD_CHECK(info.result == AUD_ERROR_CAPACITY);
    AUD_CHECK(std::string(info.message) ==
              "the samples of the instrument pin 64 KB, more than the sample cache budget of "
              "48 KB");
    // The old instrument plays on.
    f.renderOffline(4800);
    AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800), f.left.size()), 375.0, 1.0);
    AUD_CHECK_NEAR(peak(f.left, since(f, 4800), f.left.size()), kLevel, 0.002);
  }
}

// ############################################################################
// The engine's view

AUD_TEST(reports_the_release_as_its_tail) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(aud_graph_output_tail(f.graph) == 0);
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"),
                          loopInstrument(folder, "a", 375, "ampeg_release=0.5")));
  // The wait for the load reads the tails again; no commit is needed.
  AUD_CHECK(aud_graph_output_tail(f.graph) == 24000);
  // A removed node plays its release out - sfizz's release falls by 80 dB
  // over its time - instead of the engine's fade of 5 ms, and ends after
  // its tail.
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(4800);
  f.remove(node);
  f.renderOffline(28800);
  AUD_CHECK(peak(f.left, since(f, 28800, 4000), since(f, 28800, 4800)) > 0.1f * kLevel);
  AUD_CHECK(peak(f.left, since(f, 28800, 26400), f.left.size()) == 0.0f);
}

AUD_TEST(reports_the_release_of_a_flex_eg_as_its_tail) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // A flex EG in place of the ADSR (eg1_ampeg): its release is the stages
  // after its sustain point, 0.25 s and 0.25 s more as CC 1 rises
  // (ticket 27).
  AUD_CHECK(f.loadAndWait(
      node, folder.file("a.sfz"),
      loopInstrument(folder, "a", 375,
                     "ampeg_release=0.1 eg1_ampeg=1 eg1_sustain=1 eg1_time1=0.01 eg1_level1=1 "
                     "eg1_time2=0.25 eg1_time2_oncc1=0.25 eg1_level2=0.5 eg1_time3=0.25 "
                     "eg1_level3=0")));
  AUD_CHECK(aud_graph_output_tail(f.graph) == 36000);
}

AUD_TEST(renders_the_effects_in_blocks_shorter_than_the_largest) {
  // The output stage of audanika_app's instruments, a limiting compressor
  // and a gain that undoes sfizz's volume of -7.35 dB, in blocks shorter
  // than the largest of the graph (ticket 27): no check of sfizz fails and
  // writes to stderr on the audio thread - the rtsan build would catch the
  // write -, and below the threshold the gain raises the level by 7.35 dB.
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  const std::string effects =
      "<effect> bus=main type=comp comp_attack=0 comp_release=0.05 comp_ratio=50 "
      "comp_threshold=-0.5 comp_stlink=0\n"
      "<effect> bus=main type=gain gain=7.35\n";
  AUD_CHECK(f.loadAndWait(node, folder.file("a.sfz"), effects + loopInstrument(folder, "a", 375)));
  std::ostringstream captured;
  std::streambuf* const stderrBuffer = std::cerr.rdbuf(captured.rdbuf());
  f.renderOffline(256);
  f.noteOn(node, 69);
  for (const uint32_t block : {64u, 100u, 17u, 255u, 1u, 200u}) f.renderOffline(4800, block);
  std::cerr.rdbuf(stderrBuffer);
  AUD_CHECK(captured.str().empty());
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800), f.left.size()),
                 kLevel * std::pow(10.0f, 7.35f / 20), 0.002);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800), f.left.size()), 375.0, 1.0);
}

AUD_TEST(reports_a_release_beyond_any_tail_as_the_longest_tail) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  // sfizz takes releases longer than a tail counts.
  for (const std::string release : {"1e6", "1e30"}) {
    AUD_CHECK(f.loadAndWait(node, folder.file("long.sfz"),
                            loopInstrument(folder, "long", 375, "ampeg_release=" + release)));
    AUD_CHECK(aud_graph_output_tail(f.graph) == AUD_TAIL_INFINITE - 1);
  }
}

AUD_TEST(prepares_a_load_that_runs_for_the_new_format) {
  Fixture f;
  TempFolder folder;
  const int32_t node = f.sampler();
  AUD_CHECK(aud_graph_stop(f.graph) == AUD_OK);
  {
    WorkerHold hold(f.host());
    AUD_CHECK(f.loadText(node, folder.file("a.sfz"), loopInstrument(folder, "a", 375)) > 0);
    AUD_CHECK(aud_graph_prepare(f.graph, 44100, 0) == AUD_OK);
  }
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(aud_graph_start(f.graph) == AUD_OK);
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 480), f.left.size(), 44100), 375.0, 1.0);
  // A prepare between loads reaches the synth that plays.
  AUD_CHECK(aud_graph_stop(f.graph) == AUD_OK);
  AUD_CHECK(aud_graph_prepare(f.graph, 96000, 0) == AUD_OK);
  AUD_CHECK(aud_graph_start(f.graph) == AUD_OK);
  f.renderOffline(256);
  f.noteOn(node, 69);
  f.renderOffline(9600);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 9600, 960), f.left.size(), 96000), 375.0, 1.0);
}

AUD_TEST(destroys_a_graph_while_a_load_waits) {
  TempFolder folder;
  const std::string text = loopInstrument(folder, "a", 375);
  Fixture f;
  const int32_t node = f.sampler();
  WorkerHold hold(f.host());
  AUD_CHECK(f.loadText(node, folder.file("a.sfz"), text) > 0);
  // The graph destroys its nodes and then waits for the workers, which run
  // the load against a node that is gone.
  std::thread releaser([&hold] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    hold.release();
  });
  f.destroy();
  releaser.join();
}

// ############################################################################
// RealtimeSanitizer

namespace {

// aud.test.probe: allocates on the audio thread when AUD_TEST_RTSAN_PROBE
// is set; scripts/test-native.js checks that the rtsan run catches it, so
// that the run's silence about the sampler means something.
const bool g_probe = std::getenv("AUD_TEST_RTSAN_PROBE") != nullptr;
char* volatile g_probeSink = nullptr;

void* probeCreate(const AudNodeDescriptor*, const AudHostApi*) { return new int(0); }
void probeDestroy(void* instance) { delete static_cast<int*>(instance); }
int32_t probePrepare(void*, const AudPrepareInfo*) { return AUD_OK; }
void probeReset(void*, uint32_t) {}
void probeSetParam(void*, uint32_t, float) {}
void probeProcess(void*, const AudProcessContext* context) {
  if (g_probe) {
    g_probeSink = new char[16];
    delete[] g_probeSink;
  }
  for (uint32_t c = 0; c < context->outputs[0].num_channels; ++c) {
    std::memset(context->outputs[0].channels[c], 0, sizeof(float) * context->frames);
  }
}

const AudBusDescriptor kProbeOutputs[] = {
    {sizeof(AudBusDescriptor), "out", "Output", AUD_BUS_MAIN, 2, 2, 2},
};

const AudNodeVTable kProbeVTable = {
    sizeof(AudNodeVTable), probeCreate, probeDestroy, probePrepare, probeReset,
    probeSetParam,         probeProcess, nullptr,     nullptr,      nullptr,
    nullptr,               nullptr,
};

const AudNodeDescriptor kProbeDescriptor = {
    sizeof(AudNodeDescriptor),
    AUD_ABI_VERSION_MAJOR,
    AUD_ABI_VERSION_MINOR,
    1,
    "aud.test.probe",
    "Probe",
    "Test",
    AUD_NODE_CAP_VARIABLE_BLOCK,
    0,
    0,
    1,
    nullptr,
    kProbeOutputs,
    0,
    0,
    nullptr,
    nullptr,
    0,
    0,
    nullptr,
    nullptr,
    &kProbeVTable,
};

}  // namespace

AUD_TEST(rtsan_probe) {
  Fixture f;
  TempFolder folder;
  const AudHostApi* host = f.host();
  AUD_CHECK(host->register_node_type(host->host, &kProbeDescriptor) == AUD_OK);
  const int32_t sampler = f.sampler();
  const int32_t probe = aud_graph_create_node(f.graph, "aud.test.probe", nullptr);
  aud_graph_begin(f.graph);
  aud_graph_connect(f.graph, probe, 0, AUD_GRAPH_NODE, 0, 0);
  AUD_CHECK(aud_graph_commit(f.graph) > 0);
  AUD_CHECK(f.loadAndWait(sampler, folder.file("a.sfz"), loopInstrument(folder, "a", 375)));
  f.renderLive(256);
  f.noteOn(sampler, 69);
  f.renderLive(2048);
  AUD_CHECK(peak(f.left, f.left.size() - 1024, f.left.size()) > 0.5f * kLevel);
}

int main() { return aud_test::run(); }

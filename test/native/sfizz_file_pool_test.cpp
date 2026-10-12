// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the sample store and the pools of the sfizz fork
// (src/sfizz/FilePool.cpp) on synths of their own, without the node: the
// streams of files nobody holds while trims run, samples whose headers claim
// more frames than sfizz counts or memory holds, a pool without a host, and
// the samples a load cannot read, which it drops like missing ones, and
// the keys of the store, which share one file between the instruments that
// reach it by different paths (ticket 27).
//
// The pools of these synths report their bytes to nobody. Each test first
// lets the engine trim what the store holds for it, so that the store then
// holds only what the test adds, and frees that itself at its end.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aud_audio_graph.h"

#include "AudioSpan.h"
#include "FilePool.h"
#include "Synth.h"

#include "aud_directory_backend.hpp"
#include "aud_sampler_fixture.hpp"

// The allocation limit of the fixture, for the whole binary.
#if AUD_TEST_ALLOCATION_LIMIT
#if __has_feature(address_sanitizer)
extern "C" const char* __asan_default_options() {
  return "allocator_may_return_null=1:max_allocation_size_mb=1024";
}
#else
extern "C" const char* __tsan_default_options() {
  return "allocator_may_return_null=1:max_allocation_size_mb=1024";
}
#endif
#endif

using namespace aud_sampler_test;

namespace {

constexpr uint32_t kBlockFrames = 256;
constexpr uint32_t kFloatBytes = 4;

// The tasks a pool posts, run when the test says so. The user of a stream
// task is its file: while runAll() runs one, the queue notes the status the
// file has when the stream reports its bytes.
struct TaskQueue {
  std::vector<std::pair<void (*)(void*), void*>> tasks;
  sfz::FileData* streaming = nullptr;
  std::vector<sfz::FileData::Status> statusesAtReport;

  static bool post(void* context, void (*run)(void*), void* user) {
    static_cast<TaskQueue*>(context)->tasks.emplace_back(run, user);
    return true;
  }

  static void bytesChanged(void* context, int64_t) {
    auto* queue = static_cast<TaskQueue*>(context);
    if (queue->streaming != nullptr) {
      queue->statusesAtReport.push_back(queue->streaming->status.load());
    }
  }

  // A host whose worker is this queue; nobody counts its bytes.
  sfz::FileHost host() {
    sfz::FileHost fileHost;
    fileHost.context = this;
    fileHost.post = &TaskQueue::post;
    fileHost.bytesChanged = &TaskQueue::bytesChanged;
    return fileHost;
  }

  void runAll() {
    for (const auto& task : tasks) {
      streaming = static_cast<sfz::FileData*>(task.second);
      task.first(task.second);
    }
    streaming = nullptr;
    tasks.clear();
  }

  // Runs every task on a thread of its own, all at the same time, like the
  // workers of an engine.
  void runConcurrently() {
    std::vector<std::thread> threads;
    for (const auto& task : tasks) threads.emplace_back(task.first, task.second);
    for (std::thread& thread : threads) thread.join();
    tasks.clear();
  }
};

// A synth at kRate in blocks of kBlockFrames; its pool streams through
// `queue`, or reads whole files without one.
std::unique_ptr<sfz::Synth> synthOf(TaskQueue* queue = nullptr) {
  auto synth = std::make_unique<sfz::Synth>();
  synth->setSampleRate(static_cast<float>(kRate));
  synth->setSamplesPerBlock(static_cast<int>(kBlockFrames));
  if (queue != nullptr) synth->getResources().getFilePool().setHost(queue->host());
  return synth;
}

// Renders `frames` frames in blocks; returns the left channel.
std::vector<float> render(sfz::Synth& synth, uint32_t frames) {
  std::vector<float> left(frames);
  std::vector<float> right(frames);
  for (uint32_t done = 0; done < frames; done += kBlockFrames) {
    const uint32_t block = std::min(kBlockFrames, frames - done);
    float* channels[2] = {left.data() + done, right.data() + done};
    synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, block));
  }
  return left;
}

// Lets the engine trim what nobody holds; the engine counts those bytes.
void trimThroughTheEngine() {
  const uint64_t budget = aud_cache_budget();
  aud_cache_set_budget(1);
  aud_cache_set_budget(budget);
}

// Frees what the test left: nobody holds it any more.
void trimTheStore() { sfz::SampleStore::get().trim(std::numeric_limits<uint64_t>::max()); }

// Writes a mono RF64 file of 32-bit floats whose header claims `claimed`
// frames while it holds `samples`.
void writeRf64(const std::string& file, const std::vector<float>& samples, uint64_t claimed) {
  std::ofstream out(file, std::ios::binary);
  const auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
  const auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  const auto u64 = [&](uint64_t v) { out.write(reinterpret_cast<const char*>(&v), 8); };
  const uint32_t unknown = 0xFFFFFFFF;  // the 32-bit sizes of RF64
  out.write("RF64", 4);
  u32(unknown);
  out.write("WAVE", 4);
  out.write("ds64", 4);
  u32(28);  // the sizes of the file and the data, the frames, no table
  u64(unknown);
  u64(claimed * kFloatBytes);
  u64(claimed);
  u32(0);
  out.write("fmt ", 4);
  u32(16);
  u16(3);  // IEEE float
  u16(1);
  u32(static_cast<uint32_t>(kRate));
  u32(static_cast<uint32_t>(kRate) * kFloatBytes);
  u16(kFloatBytes);
  u16(32);
  out.write("data", 4);
  u32(unknown);
  out.write(reinterpret_cast<const char*>(samples.data()),
            static_cast<std::streamsize>(samples.size() * kFloatBytes));
}

// The bytes of a file.
std::vector<char> bytesOf(const std::string& file) {
  std::ifstream in(file, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The Base64 text of `bytes`, as a <sample> header embeds a file.
std::string base64Of(const std::vector<char>& bytes) {
  static const char kDigits[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  for (size_t i = 0; i < bytes.size(); i += 3) {
    const size_t left = bytes.size() - i;
    uint32_t chunk = static_cast<uint32_t>(static_cast<uint8_t>(bytes[i])) << 16;
    if (left > 1) chunk |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[i + 1])) << 8;
    if (left > 2) chunk |= static_cast<uint8_t>(bytes[i + 2]);
    text += kDigits[(chunk >> 18) & 63];
    text += kDigits[(chunk >> 12) & 63];
    text += left > 1 ? kDigits[(chunk >> 6) & 63] : '=';
    text += left > 2 ? kDigits[chunk & 63] : '=';
  }
  return text;
}

// The region a load keeps beside the one it drops on key 60: a sine of
// 375 Hz on key 61, short enough for its head.
std::string keptRegion(const TempFolder& folder) {
  writeWav(folder.file("kept.wav"), sine(375, 0.1));
  return "<region> sample=kept.wav key=61\n";
}

// Checks a synth whose load dropped the region of `missing` on key 60 and
// kept the one of keptRegion(): the sample is named, a note on key 60 starts
// no voice and stays silent, a note on key 61 sounds.
void checkDropped(sfz::Synth& synth, const std::string& missing) {
  AUD_CHECK(synth.getMissingSamples() == std::vector<std::string>{missing});
  AUD_CHECK(synth.getNumRegions() == 1);
  synth.noteOn(0, 60, 100);
  AUD_CHECK(synth.getNumActiveVoices() == 0);
  AUD_CHECK(peak(render(synth, 4800), 0, 4800) == 0.0f);
  synth.noteOn(0, 61, 100);
  AUD_CHECK(synth.getNumActiveVoices() == 1);
  const std::vector<float> kept = render(synth, 4800);
  AUD_CHECK_NEAR(frequencyOf(kept, 480, 4000), 375.0, 1.0);
}

}  // namespace

// ############################################################################
// Streams and trims

AUD_TEST(sfizz_store_counts_a_stream_before_a_trim_may_free_it) {
  trimThroughTheEngine();
  TempFolder folder;
  writeWav(folder.file("counted.wav"), sine(375, 1.0));
  TaskQueue queue;
  std::unique_ptr<sfz::Synth> synth = synthOf(&queue);
  AUD_CHECK(synth->loadSfzString(folder.file("counted.sfz"),
                                 "<region> sample=counted.wav pitch_keycenter=69\n"));
  synth->noteOn(0, 69, 100);
  render(*synth, kBlockFrames);
  AUD_CHECK(queue.tasks.size() == 1);
  queue.runAll();
  // The stream reports its bytes while its file still streams, which a trim
  // leaves alone: once the status reads Done, a trim may free the file the
  // report would read.
  AUD_CHECK(queue.statusesAtReport ==
            std::vector<sfz::FileData::Status>{sfz::FileData::Status::Streaming});
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_store_streams_files_nobody_holds_while_it_trims) {
  trimThroughTheEngine();
  TempFolder folder;
  constexpr int kFiles = 16;
  constexpr int kFirstKey = 48;
  std::string text;
  for (int i = 0; i < kFiles; ++i) {
    const std::string name = "stream" + std::to_string(i) + ".wav";
    writeWav(folder.file(name), sine(375, 1.0));
    text += "<region> sample=" + name + " key=" + std::to_string(kFirstKey + i) + "\n";
  }
  const int64_t bytesBefore = sfz::SampleStore::get().bytes();
  const size_t filesBefore = sfz::SampleStore::get().size();
  TaskQueue queue;
  {
    std::unique_ptr<sfz::Synth> synth = synthOf(&queue);
    AUD_CHECK(synth->loadSfzString(folder.file("streams.sfz"), text));
    for (int i = 0; i < kFiles; ++i) synth->noteOn(0, kFirstKey + i, 100);
    render(*synth, kBlockFrames);
  }
  // The synth is gone: nobody holds the files whose streams wait. They
  // stream at the same time while another thread trims the store all the
  // time; no stream touches its file once a trim may free it, and every
  // byte a stream adds is freed once.
  AUD_CHECK(queue.tasks.size() == static_cast<size_t>(kFiles));
  std::atomic<bool> streaming{true};
  std::thread trimmer([&streaming] {
    while (streaming.load()) trimTheStore();
  });
  queue.runConcurrently();
  streaming.store(false);
  trimmer.join();
  trimTheStore();
  AUD_CHECK(sfz::SampleStore::get().bytes() == bytesBefore);
  AUD_CHECK(sfz::SampleStore::get().size() == filesBefore);
}

// ############################################################################
// Headers that claim too much

AUD_TEST(sfizz_pool_refuses_a_sample_longer_than_sfizz_counts) {
  trimThroughTheEngine();
  TempFolder folder;
  // 2^40 frames, as a 36-bit FLAC or an RF64 header may claim.
  const uint64_t claimed = uint64_t{1} << 40;
  writeRf64(folder.file("huge.wav"), sine(375, 0.5), claimed);
  writeWav(folder.file("fine.wav"), sine(375, 0.5));
  std::unique_ptr<sfz::Synth> synth = synthOf();
  AUD_CHECK(synth->loadSfzString(folder.file("huge.sfz"),
                                 "<region> sample=huge.wav key=60\n"
                                 "<region> sample=fine.wav key=61\n"));
  AUD_CHECK(synth->getMissingSamples() == std::vector<std::string>{"huge.wav"});
  AUD_CHECK(synth->getNumRegions() == 1);
  synth.reset();
  trimTheStore();
}

#if AUD_TEST_ALLOCATION_LIMIT
AUD_TEST(sfizz_pool_plays_the_head_of_a_sample_that_does_not_fit_into_memory) {
  trimThroughTheEngine();
  TempFolder folder;
  // 4 GB of frames claimed, 20000 frames there: the head loads, the stream
  // cannot allocate the whole file.
  const std::vector<float> samples = sine(375, 20000 / kRate);
  writeClaimingWav(folder.file("claims.wav"), samples, 0xFFFFFFF0u);
  TaskQueue queue;
  std::unique_ptr<sfz::Synth> synth = synthOf(&queue);
  AUD_CHECK(synth->loadSfzString(folder.file("claims.sfz"),
                                 "<region> sample=claims.wav pitch_keycenter=69\n"));
  AUD_CHECK(synth->getNumRegions() == 1);
  synth->noteOn(0, 69, 100);
  std::vector<float> left = render(*synth, kBlockFrames);
  AUD_CHECK(queue.tasks.size() == 1);
  queue.runAll();
  // The voice plays the head, then silence, and asks for the stream no more.
  const std::vector<float> rest = render(*synth, kHeadFrames + 4096);
  left.insert(left.end(), rest.begin(), rest.end());
  AUD_CHECK(queue.tasks.empty());
  AUD_CHECK(peak(left, 480, 8000) > 0.01f);
  AUD_CHECK(peak(left, kHeadFrames + 64, left.size()) == 0.0f);
  AUD_CHECK(synth->getResources().getFilePool().takeStreamFailures() == 0);
  synth.reset();
  trimTheStore();
}
#endif

// ############################################################################
// A pool without a host

AUD_TEST(sfizz_pool_without_a_host_reads_whole_files) {
  trimThroughTheEngine();
  TempFolder folder;
  writeWav(folder.file("whole.wav"), sine(375, 1.0));
  std::unique_ptr<sfz::Synth> synth = synthOf();
  AUD_CHECK(synth->loadSfzString(folder.file("whole.sfz"),
                                 "<region> sample=whole.wav pitch_keycenter=69\n"));
  synth->noteOn(0, 69, 100);
  const std::vector<float> left = render(*synth, 48000);
  // The whole second sounds, past the head, without a stream.
  AUD_CHECK_NEAR(frequencyOf(left, kHeadFrames + 4800, 44000), 375.0, 1.0);
  AUD_CHECK(peak(left, kHeadFrames + 4800, 44000) > 0.5f * peak(left, 480, 8000));
  sfz::FilePool& pool = synth->getResources().getFilePool();
  AUD_CHECK(pool.takeStreamFailures() == 0);
  AUD_CHECK(pool.takeUnderflows() == 0);
  synth.reset();
  trimTheStore();
}

// ############################################################################
// Samples a load cannot read

#if AUD_TEST_ALLOCATION_LIMIT
AUD_TEST(sfizz_pool_without_a_host_drops_a_sample_that_does_not_fit_into_memory) {
  trimThroughTheEngine();
  TempFolder folder;
  // 4 GB of frames claimed: without a host, the pool reads whole files.
  writeClaimingWav(folder.file("claims.wav"), sine(375, 0.5), 0xFFFFFFF0u);
  std::unique_ptr<sfz::Synth> synth = synthOf();
  AUD_CHECK(synth->loadSfzString(folder.file("claims.sfz"),
                                 "<region> sample=claims.wav key=60\n" + keptRegion(folder)));
  checkDropped(*synth, "claims.wav");
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_pool_drops_a_sample_whose_head_does_not_fit_into_memory) {
  trimThroughTheEngine();
  TempFolder folder;
  // 4 GB of frames claimed, and an offset that asks for a head of 300
  // million of them.
  writeClaimingWav(folder.file("claims.wav"), sine(375, 0.5), 0xFFFFFFF0u);
  TaskQueue queue;
  std::unique_ptr<sfz::Synth> synth = synthOf(&queue);
  AUD_CHECK(synth->loadSfzString(
      folder.file("claims.sfz"),
      "<region> sample=claims.wav key=60 offset=300000000\n" + keptRegion(folder)));
  checkDropped(*synth, "claims.wav");
  AUD_CHECK(queue.tasks.empty());
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_pool_drops_an_oscillator_whose_file_does_not_fit_into_memory) {
  trimThroughTheEngine();
  TempFolder folder;
  // A wavetable reads its file whole.
  writeClaimingWav(folder.file("claims.wav"), sine(375, 0.5), 0xFFFFFFF0u);
  TaskQueue queue;
  std::unique_ptr<sfz::Synth> synth = synthOf(&queue);
  AUD_CHECK(synth->loadSfzString(
      folder.file("claims.sfz"),
      "<region> sample=claims.wav key=60 oscillator=on\n" + keptRegion(folder)));
  checkDropped(*synth, "claims.wav");
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_pool_drops_an_embedded_sample_that_does_not_fit_into_memory) {
  trimThroughTheEngine();
  TempFolder folder;
  // The text embeds a file whose header claims 4 GB of frames; no file
  // beside the text has its name.
  writeClaimingWav(folder.file("source.wav"), sine(375, 0.5), 0xFFFFFFF0u);
  const std::string data = base64Of(bytesOf(folder.file("source.wav")));
  std::unique_ptr<sfz::Synth> synth = synthOf();
  AUD_CHECK(synth->loadSfzString(folder.file("embedded.sfz"),
                                 "<sample> name=embedded.wav base64data=" + data + "\n" +
                                     "<region> sample=embedded.wav key=60\n" +
                                     keptRegion(folder)));
  checkDropped(*synth, "embedded.wav");
  synth.reset();
  trimTheStore();
}
#endif

AUD_TEST(sfizz_pool_drops_a_sample_gone_before_its_file_loads) {
  trimThroughTheEngine();
  TempFolder folder;
  const std::string gone = folder.file("gone.wav");
  writeWav(gone, sine(375, 0.5));
  std::unique_ptr<sfz::Synth> synth = synthOf();
  sfz::FilePool& pool = synth->getResources().getFilePool();
  // The files load in the second half of the progress: the sample is gone
  // after the load checked it and before its file loads.
  pool.setLoadObserver([&gone](float progress) {
    if (progress >= 0.5f) std::remove(gone.c_str());
    return true;
  });
  AUD_CHECK(synth->loadSfzString(folder.file("gone.sfz"),
                                 "<region> sample=gone.wav key=60\n" + keptRegion(folder)));
  pool.setLoadObserver(nullptr);
  checkDropped(*synth, "gone.wav");
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_pool_drops_a_short_sample_it_cannot_read_whole) {
  trimThroughTheEngine();
  TempFolder folder;
  // Fewer frames than a wavetable: the load reads the sample whole to see
  // whether it is silent.
  const std::string file = folder.file("short.wav");
  writeWav(file, sine(375, 0.05));
  const std::string text = "<region> sample=short.wav key=60\n" + keptRegion(folder);
  // A first load reads the information of the sample, which the store keeps
  // by the path, size and time of the file.
  AUD_CHECK(synthOf()->loadSfzString(folder.file("short.sfz"), text));
  trimTheStore();
  // Garbage of the same size and time: the information stays, the read
  // fails.
  const fs::file_time_type time = fs::last_write_time(file);
  writeText(file, std::string(static_cast<size_t>(fs::file_size(file)), 'x'));
  fs::last_write_time(file, time);
  std::unique_ptr<sfz::Synth> synth = synthOf();
  AUD_CHECK(synth->loadSfzString(folder.file("short.sfz"), text));
  checkDropped(*synth, "short.wav");
  synth.reset();
  trimTheStore();
}

// ############################################################################
// The keys of the store

AUD_TEST(sfizz_store_shares_a_sample_two_instruments_reach_by_different_paths) {
  trimThroughTheEngine();
  TempFolder folder;
  folder.createFolder("samplesets/x/samples");
  folder.createFolder("presets/a");
  folder.createFolder("presets/b/deep");
  writeWav(folder.file("samplesets/x/samples/060.wav"), sine(375, 0.1));
  writeText(folder.file("presets/a/main.sfz"),
            "<region> sample=../../samplesets/x/samples/060.wav key=60\n");
  writeText(folder.file("presets/b/deep/main.sfz"),
            "<control> default_path=../../../samplesets/x/\n"
            "<region> sample=./samples/060.wav key=60\n");
  const size_t before = sfz::SampleStore::get().size();
  std::unique_ptr<sfz::Synth> a = synthOf();
  std::unique_ptr<sfz::Synth> b = synthOf();
  AUD_CHECK(a->loadSfzFile(folder.file("presets/a/main.sfz")));
  AUD_CHECK(b->loadSfzFile(folder.file("presets/b/deep/main.sfz")));
  AUD_CHECK(a->getNumRegions() == 1 && b->getNumRegions() == 1);
  // One file in the store for both.
  AUD_CHECK(sfz::SampleStore::get().size() == before + 1);
  a.reset();
  b.reset();
  trimTheStore();
}

AUD_TEST(sfizz_store_shares_an_apk_sample_two_instruments_reach_by_different_paths) {
  trimThroughTheEngine();
  TempFolder folder;
  folder.createFolder("content/samplesets/x/samples");
  folder.createFolder("content/presets/a");
  folder.createFolder("content/presets/b");
  writeWav(folder.file("content/samplesets/x/samples/060.wav"), sine(375, 0.1));
  writeText(folder.file("content/presets/a/main.sfz"),
            "<region> sample=../../samplesets/x/samples/060.wav key=60\n");
  writeText(folder.file("content/presets/b/main.sfz"),
            "<control> default_path=../../samplesets/x/\n"
            "<region> sample=./samples/060.wav key=60\n");
  ScopedApkAssets assets(folder.path);
  const size_t before = sfz::SampleStore::get().size();
  std::unique_ptr<sfz::Synth> a = synthOf();
  std::unique_ptr<sfz::Synth> b = synthOf();
  AUD_CHECK(a->loadSfzFile("apk:/content/presets/a/main.sfz"));
  AUD_CHECK(b->loadSfzFile("apk:/content/presets/b/main.sfz"));
  AUD_CHECK(a->getNumRegions() == 1 && b->getNumRegions() == 1);
  AUD_CHECK(sfz::SampleStore::get().size() == before + 1);
  a.reset();
  b.reset();
  trimTheStore();
}

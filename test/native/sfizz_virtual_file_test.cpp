// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the virtual files of the sfizz fork (src/sfizz/VirtualFile.cpp,
// ticket 27): the paths "<scheme>:/<path>" and their normalization, and
// what sfizz reads through a backend - an SFZ file with its includes,
// default_path and samples, and a Scala file - on synths of their own,
// with the folder of the host that aud_directory_backend.hpp serves as the
// assets below "apk:/".

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "AudioSpan.h"
#include "FilePool.h"
#include "Synth.h"
#include "VirtualFile.h"

#include "aud_directory_backend.hpp"
#include "aud_sampler_fixture.hpp"

using namespace aud_sampler_test;
using sfz::VirtualFiles;

namespace {

constexpr uint32_t kBlockFrames = 256;

std::unique_ptr<sfz::Synth> synthOf() {
  auto synth = std::make_unique<sfz::Synth>();
  synth->setSampleRate(static_cast<float>(kRate));
  synth->setSamplesPerBlock(static_cast<int>(kBlockFrames));
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

// Frees what the test left: nobody holds it any more.
void trimTheStore() { sfz::SampleStore::get().trim(std::numeric_limits<uint64_t>::max()); }

// An instrument laid out like the app's content: the preset in
// presets/x/main.sfz includes a file two folders up and plays a sample
// reached through default_path with "..".
void writeContent(const TempFolder& folder) {
  folder.createFolder("content/sfz/presets/x");
  folder.createFolder("content/sfz/samples/piano");
  writeWav(folder.file("content/sfz/samples/piano/A4.wav"), sine(440, 0.5));
  writeText(folder.file("content/sfz/common.sfz"), "<control> default_path=samples/piano/\n");
  writeText(folder.file("content/sfz/presets/x/main.sfz"),
            "#include \"../../common.sfz\"\n"
            "<control> default_path=../../samples/piano/\n"
            "<region> sample=a4.wav pitch_keycenter=69\n");
}

}  // namespace

// ############################################################################
// Paths

AUD_TEST(sfizz_virtual_paths_have_a_scheme_of_at_least_two_letters) {
  AUD_CHECK(VirtualFiles::isVirtual("apk:/content/x.sfz"));
  AUD_CHECK(VirtualFiles::isVirtual("apk:"));
  AUD_CHECK(VirtualFiles::isVirtual("apk:/"));
  AUD_CHECK(VirtualFiles::isVirtual("my-scheme.2:/x"));
  AUD_CHECK(!VirtualFiles::isVirtual("C:/x.sfz"));
  AUD_CHECK(!VirtualFiles::isVirtual("/content/x.sfz"));
  AUD_CHECK(!VirtualFiles::isVirtual("content/x.sfz"));
  AUD_CHECK(!VirtualFiles::isVirtual("apk:x.sfz"));
  AUD_CHECK(!VirtualFiles::isVirtual("1pk:/x.sfz"));
  AUD_CHECK(!VirtualFiles::isVirtual(""));
  AUD_CHECK(VirtualFiles::schemeOf("apk:/x") == "apk");
  AUD_CHECK(VirtualFiles::schemeOf("/x").empty());
}

AUD_TEST(sfizz_virtual_paths_resolve_dots_and_double_slashes) {
  AUD_CHECK(VirtualFiles::pathOf("apk:/content/sfz/presets/x/../../samples/a.wav") ==
            "content/sfz/samples/a.wav");
  AUD_CHECK(VirtualFiles::pathOf("apk:/content//sfz/./a.sfz") == "content/sfz/a.sfz");
  AUD_CHECK(VirtualFiles::pathOf("apk:/../a.sfz") == "a.sfz");
  AUD_CHECK(VirtualFiles::pathOf("apk:/content/..") == "");
  AUD_CHECK(VirtualFiles::pathOf("apk:/") == "");
  AUD_CHECK(VirtualFiles::pathOf("apk:") == "");
  AUD_CHECK(VirtualFiles::pathOf("/content/a.sfz") == "");
  AUD_CHECK(VirtualFiles::normalized("apk:/content/x/../a.sfz") == "apk:/content/a.sfz");
}

AUD_TEST(sfizz_virtual_files_come_from_the_backend_of_their_scheme) {
  TempFolder folder;
  writeText(folder.file("a.txt"), "hello");
  AUD_CHECK(!VirtualFiles::backendOf("apk:/a.txt"));
  AUD_CHECK(VirtualFiles::sizeOf("apk:/a.txt") == -1);
  AUD_CHECK(!VirtualFiles::open("apk:/a.txt"));
  {
    ScopedApkAssets assets(folder.path);
    AUD_CHECK(VirtualFiles::backendOf("apk:/a.txt") == assets.backend);
    AUD_CHECK(!VirtualFiles::backendOf("ipa:/a.txt"));
    AUD_CHECK(VirtualFiles::sizeOf("apk:/a.txt") == 5);
    AUD_CHECK(VirtualFiles::sizeOf("apk:/x/../a.txt") == 5);
    AUD_CHECK(VirtualFiles::exists("apk:/a.txt"));
    AUD_CHECK(!VirtualFiles::exists("apk:/b.txt"));
    AUD_CHECK(!VirtualFiles::exists("apk:/"));
    const std::unique_ptr<sfz::VirtualFileData> data = VirtualFiles::open("apk:/a.txt");
    AUD_CHECK(data && std::string(static_cast<const char*>(data->data()), data->size()) == "hello");
    AUD_CHECK(!VirtualFiles::open("apk:/b.txt"));
  }
  AUD_CHECK(!VirtualFiles::backendOf("apk:/a.txt"));
}

AUD_TEST(sfizz_virtual_files_match_the_file_name_without_regard_to_case) {
  TempFolder folder;
  folder.createFolder("Samples");
  writeText(folder.file("Samples/A4.wav"), "x");
  ScopedApkAssets assets(folder.path);
  std::string name = "Samples/A4.wav";
  AUD_CHECK(VirtualFiles::checkFile("apk:/", name) && name == "Samples/A4.wav");
  name = "../Samples/a4.WAV";
  AUD_CHECK(VirtualFiles::checkFile("apk:/presets", name) && name == "../Samples/A4.wav");
  name = "A4.wav";
  AUD_CHECK(VirtualFiles::checkFile("apk:/Samples", name) && name == "A4.wav");
  // The directories must match exactly: an APK lists files only.
  name = "samples/a4.wav";
  AUD_CHECK(!VirtualFiles::checkFile("apk:/", name) && name == "samples/a4.wav");
  name = "B4.wav";
  AUD_CHECK(!VirtualFiles::checkFile("apk:/Samples", name));
  AUD_CHECK(VirtualFiles::equalsIgnoreCase("aB.Wav", "Ab.wAV"));
  AUD_CHECK(!VirtualFiles::equalsIgnoreCase("ab.wav", "ab.wa"));
}

// ############################################################################
// What sfizz reads through a backend

AUD_TEST(sfizz_loads_an_instrument_with_its_includes_and_samples_from_apk_assets) {
  TempFolder folder;
  writeContent(folder);
  ScopedApkAssets assets(folder.path);
  auto synth = synthOf();
  AUD_CHECK(synth->loadSfzFile("apk:/content/sfz/presets/x/main.sfz"));
  AUD_CHECK(synth->getNumRegions() == 1);
  AUD_CHECK(synth->getMissingSamples().empty());
  AUD_CHECK(synth->getParseErrors().empty());
  // The SFZ file, its include, and the sample twice: sfizz reads its
  // information, then its data; all of them below the backend's root.
  AUD_CHECK(assets.backend->openedPaths ==
            std::vector<std::string>({"content/sfz/presets/x/main.sfz", "content/sfz/common.sfz",
                                      "content/sfz/samples/piano/A4.wav",
                                      "content/sfz/samples/piano/A4.wav"}));
  synth->noteOn(0, 69, 100);
  const std::vector<float> left = render(*synth, 4800);
  AUD_CHECK_NEAR(frequencyOf(left, 480, 4800), 440.0, 1.0);
  AUD_CHECK(peak(left, 480, 4800) > 0.01f);
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_names_a_sample_an_apk_does_not_hold) {
  TempFolder folder;
  writeContent(folder);
  writeText(folder.file("content/sfz/presets/x/main.sfz"),
            "<region> sample=../../samples/piano/A4.wav key=60\n"
            "<region> sample=../../samples/piano/B4.wav key=62\n");
  ScopedApkAssets assets(folder.path);
  auto synth = synthOf();
  AUD_CHECK(synth->loadSfzFile("apk:/content/sfz/presets/x/main.sfz"));
  AUD_CHECK(synth->getNumRegions() == 1);
  AUD_CHECK(synth->getMissingSamples() ==
            std::vector<std::string>{"../../samples/piano/B4.wav"});
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_fails_an_apk_path_without_a_backend_and_says_so) {
  TempFolder folder;
  writeContent(folder);
  auto synth = synthOf();
  AUD_CHECK(!synth->loadSfzFile("apk:/content/sfz/presets/x/main.sfz"));
  AUD_CHECK(synth->getParseErrors().size() == 1);
  AUD_CHECK(synth->getParseErrors().front() ==
            "main.sfz at line 1: No backend serves apk:/ paths on this platform: "
            "apk:/content/sfz/presets/x/main.sfz");
  AUD_CHECK(!synth->loadScalaFile("apk:/content/wide.scl"));
}

AUD_TEST(sfizz_loads_a_scala_file_from_apk_assets) {
  TempFolder folder;
  writeContent(folder);
  writeText(folder.file("content/sfz/wide.scl"),
            "! wide.scl\nTwelve steps of 110 cents\n12\n110.\n220.\n330.\n440.\n550.\n660.\n"
            "770.\n880.\n990.\n1100.\n1210.\n1320.\n");
  ScopedApkAssets assets(folder.path);
  auto synth = synthOf();
  AUD_CHECK(synth->loadSfzFile("apk:/content/sfz/presets/x/main.sfz"));
  AUD_CHECK(synth->loadScalaFile("apk:/content/sfz/wide.scl"));
  synth->noteOn(0, 70, 100);
  const std::vector<float> left = render(*synth, 9600);
  AUD_CHECK_NEAR(frequencyOf(left, 480, 9600), 440.0 * std::pow(2.0, 1.1 / 12.0), 1.5);
  AUD_CHECK(!synth->loadScalaFile("apk:/content/sfz/missing.scl"));
  synth.reset();
  trimTheStore();
}

AUD_TEST(sfizz_streams_an_apk_sample_through_the_backend) {
  TempFolder folder;
  folder.createFolder("content");
  // Longer than the head of 8192 frames, so that a note streams the rest.
  writeWav(folder.file("content/long.wav"), sine(440, 1.0));
  writeText(folder.file("content/long.sfz"), "<region> sample=long.wav pitch_keycenter=69\n");
  ScopedApkAssets assets(folder.path);
  auto synth = synthOf();
  AUD_CHECK(synth->loadSfzFile("apk:/content/long.sfz"));
  // Without a host the pool reads the whole file at once: the SFZ file, the
  // information of the sample and its data.
  AUD_CHECK(assets.backend->opens == 3);
  synth->noteOn(0, 69, 100);
  const std::vector<float> left = render(*synth, 24000);
  AUD_CHECK_NEAR(frequencyOf(left, 12000, 24000), 440.0, 1.0);
  AUD_CHECK(peak(left, 12000, 24000) > 0.01f);
  synth.reset();
  trimTheStore();
}

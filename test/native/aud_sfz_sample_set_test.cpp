// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The native tests of presets that share a sample set (ticket 31): a set
// holds its regions once, and every preset that plays it includes them.
// The layout below is loaded from files, from the assets of an APK
// (sampler-004, a folder of the host as the assets) and as SFZ text whose
// sfz_virtual_path resolves its includes:
//
//   common/engine.sfz              <control> without default_path, then an
//                                  #include of common/level.sfz written
//                                  relative to the root file
//   common/level.sfz               <global> amplitude=50
//   samplesets/sines/regions.sfz   the regions of the set only
//   samplesets/sines/instrument.sfz  the set alone
//   samplesets/sines/samples/      low.wav, 750 Hz; high.flac, 375 Hz,
//                                  looped
//   presets/<name>/main.sfz        #define $SET and $SHIFT, the include of
//                                  common/engine.sfz, a <control> with
//                                  default_path=../../samplesets/$SET/ and
//                                  note_offset=$SHIFT, a <group>, the
//                                  include of the set's regions
//
// sfizz resolves an #include against the folder of the root file, also in
// a file that is itself included, and a sample against that folder and the
// default_path the last <control> set; every <control> sets default_path
// back to none.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "aud_directory_backend.hpp"
#include "aud_sampler_fixture.hpp"

using namespace aud_sampler_test;

namespace {

// The level of the presets: kLevel, halved by amplitude=50 of
// common/level.sfz.
constexpr float kHalfLevel = kLevel / 2;
// Below it a note counts as silent.
constexpr float kSilence = 1e-4f;

// The frames of high.flac: 100 periods of 375 Hz. Its loop is the second
// half, 50 periods, so that the loop has no seam.
constexpr uint32_t kHighFrames = 12800;
constexpr uint32_t kLoopStart = 6400;
constexpr uint32_t kLoopEnd = kHighFrames - 1;

// A sine of `frequency` Hz over `frames`.
std::vector<float> sineFrames(double frequency, uint32_t frames) {
  return sine(frequency, frames / kRate);
}

// The loop of high.flac as opcodes.
const std::string kLoop = "loop_mode=loop_continuous loop_start=" + std::to_string(kLoopStart) +
                          " loop_end=" + std::to_string(kLoopEnd);

// The regions of the set: the low one from key 0 to 63 sounds 750 Hz on
// key 12, the high one from 64 to 127 sounds 375 Hz on key 115 and loops.
const std::string kRegions =
    "// The regions of the set only; the samples resolve against the\n"
    "// default_path of the file that includes them.\n"
    "<region> sample=samples/low.wav lokey=0 hikey=63 pitch_keycenter=12\n"
    "<region> sample=samples/high.flac lokey=64 hikey=127 pitch_keycenter=115 " +
    kLoop + "\n";

// A preset of the set $SET shifted by $SHIFT keys, with more control
// opcodes in `control`.
std::string presetText(const std::string& set, int shift, const std::string& control = "") {
  return "#define $SET " + set + "\n#define $SHIFT " + std::to_string(shift) +
         "\n"
         "#include \"../../common/engine.sfz\"\n"
         "\n"
         "<control>\n"
         "default_path=../../samplesets/$SET/\n"
         "note_offset=$SHIFT\n" +
         control +
         "\n"
         "<group> ampeg_release=0.01\n"
         "#include \"../../samplesets/$SET/regions.sfz\"\n";
}

// Writes the layout into `folder`, with a preset for every shift and bound
// the tests play.
void writeLayout(const TempFolder& folder) {
  for (const char* name : {"common", "samplesets/sines/samples", "presets/up", "presets/up-open",
                           "presets/down", "presets/down-open", "presets/reset",
                           "presets/gone"}) {
    folder.createFolder(name);
  }
  writeText(folder.file("common/engine.sfz"),
            "<control> label_cc7=Volume\n"
            "// Relative to the root file: from common/ this path leads nowhere.\n"
            "#include \"../../common/level.sfz\"\n");
  writeText(folder.file("common/level.sfz"), "<global> amplitude=50\n");
  writeText(folder.file("samplesets/sines/regions.sfz"), kRegions);
  writeText(folder.file("samplesets/sines/instrument.sfz"),
            "<group> ampeg_release=0.01\n#include \"regions.sfz\"\n");
  writeWav(folder.file("samplesets/sines/samples/low.wav"), sine(750, 1.0));
  writeFlac(folder.file("samplesets/sines/samples/high.flac"), sineFrames(375, kHighFrames));
  writeText(folder.file("presets/up/main.sfz"), presetText("sines", -12));
  writeText(folder.file("presets/up-open/main.sfz"),
            presetText("sines", -12, "note_offset_open_bounds=on\n"));
  writeText(folder.file("presets/down/main.sfz"), presetText("sines", 12));
  writeText(folder.file("presets/down-open/main.sfz"),
            presetText("sines", 12, "note_offset_open_bounds=on\n"));
  // The <control> of common/engine.sfz comes after the one that sets
  // default_path and sets it back to none.
  writeText(folder.file("presets/reset/main.sfz"),
            "#define $SET sines\n"
            "<control> default_path=../../samplesets/$SET/\n"
            "#include \"../../common/engine.sfz\"\n"
            "<group> ampeg_release=0.01\n"
            "#include \"../../samplesets/$SET/regions.sfz\"\n");
  writeText(folder.file("presets/gone/main.sfz"), presetText("gone", 0));
}

// How the tests load a preset of the layout: as a file of the host, as a
// file of the APK's assets, or as text with a virtual path of either.
enum class Via { file, apk, text, apkText };

const char* nameOf(Via via) {
  switch (via) {
    case Via::file:
      return "file";
    case Via::apk:
      return "apk";
    case Via::text:
      return "text";
    case Via::apkText:
      return "apk text";
  }
  return "";
}

// The path a load of `relative`, e.g. "presets/up/main.sfz", names: the
// file, or the virtual path of the text.
std::string pathOf(const TempFolder& folder, Via via, const std::string& relative) {
  return via == Via::file || via == Via::text ? folder.file(relative) : "apk:/" + relative;
}

// Loads the preset `relative` of the layout and waits; returns the load.
AudLoadInfo loadPreset(Fixture& f, int32_t node, const TempFolder& folder, Via via,
                       const std::string& relative) {
  const std::string path = pathOf(folder, via, relative);
  int32_t id = 0;
  if (via == Via::file || via == Via::apk) {
    id = f.loadFile(node, path);
  } else {
    // The text of the file, loaded as text: the file need not exist.
    std::ifstream in(folder.file(relative), std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    id = f.loadText(node, path, text);
  }
  AUD_CHECK(id > 0);
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  return f.load(id);
}

// What a note plays: its frequency and level over `frames` after its first
// 10 ms. The note is released afterwards and its release of 10 ms ends.
struct Played {
  double frequency;
  float level;
};

Played play(Fixture& f, int32_t node, uint32_t note, uint32_t frames = 9600) {
  f.noteOn(node, note);
  f.renderOffline(frames);
  const size_t from = since(f, frames, 480);
  const Played played{frequencyOf(f.left, from, f.left.size()), peak(f.left, from, f.left.size())};
  f.noteOff(node, note);
  f.renderOffline(4800);
  return played;
}

// Whether `note` sounds at `frequency` (within 0.5 %) at the level of the
// presets.
bool sounds(Fixture& f, int32_t node, uint32_t note, double frequency) {
  const Played played = play(f, node, note);
  const bool ok = std::fabs(played.frequency - frequency) <= frequency * 0.005 &&
                  std::fabs(played.level - kHalfLevel) <= 0.003f;
  if (!ok) {
    std::printf("  note %u: %.2f Hz at %.4f, not %.2f Hz at %.4f\n", note, played.frequency,
                played.level, frequency, kHalfLevel);
  }
  return ok;
}

bool silent(Fixture& f, int32_t node, uint32_t note) {
  const Played played = play(f, node, note);
  if (played.level >= kSilence) std::printf("  note %u sounds at %.4f\n", note, played.level);
  return played.level < kSilence;
}

// The first `count` paths the backend opened, fewer when it opened fewer.
std::vector<std::string> firstOf(const std::vector<std::string>& paths, size_t count) {
  const auto end = paths.begin() + static_cast<std::ptrdiff_t>(std::min(count, paths.size()));
  return std::vector<std::string>(paths.begin(), end);
}

// The frequency of `semitones` above a sample of `frequency` Hz.
double shifted(double frequency, int semitones) {
  return frequency * std::pow(2.0, semitones / 12.0);
}

// Plays the presets of the layout through `via`.
void playsThePresets(Via via) {
  const int failuresBefore = aud_test::failures();
  Fixture f;
  TempFolder folder;
  writeLayout(folder);
  ScopedApkAssets assets(folder.path);
  const int32_t node = f.sampler();
  f.renderOffline(256);

  // Transposed up by 12: note_offset=-12. Key n plays the sound of n + 12;
  // the keys that left the top of the high region fall silent.
  AudLoadInfo info = loadPreset(f, node, folder, via, "presets/up/main.sfz");
  AUD_CHECK(info.result == AUD_OK);
  AUD_CHECK(std::string(info.message).empty());
  f.renderOffline(256);
  AUD_CHECK(sounds(f, node, 0, 750));    // the low region, key 12
  AUD_CHECK(sounds(f, node, 12, 1500));  // key 24
  AUD_CHECK(sounds(f, node, 103, 375));  // the high region, key 115
  AUD_CHECK(sounds(f, node, 115, 750));  // its top, key 127
  AUD_CHECK(silent(f, node, 116));
  AUD_CHECK(silent(f, node, 127));
  // The FLAC sample loops: 1 s after the note-on its 12800 frames would have
  // ended long ago.
  f.noteOn(node, 103);
  f.renderOffline(48000);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800), f.left.size()), kHalfLevel, 0.003);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800), f.left.size()), 375.0, 1.0);
  f.noteOff(node, 103);
  f.renderOffline(4800);

  // With note_offset_open_bounds the high region still ends at key 127: its
  // new keys play above its key center like the others.
  info = loadPreset(f, node, folder, via, "presets/up-open/main.sfz");
  AUD_CHECK(info.result == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(sounds(f, node, 0, 750));
  AUD_CHECK(sounds(f, node, 103, 375));
  AUD_CHECK(sounds(f, node, 116, shifted(375, 13)));
  AUD_CHECK(sounds(f, node, 127, shifted(375, 24)));

  // Transposed down by 12: note_offset=12. The keys below the low region
  // fall silent, unless the bounds stay open; then they play below its key
  // center.
  info = loadPreset(f, node, folder, via, "presets/down/main.sfz");
  AUD_CHECK(info.result == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(silent(f, node, 0));
  AUD_CHECK(silent(f, node, 11));
  AUD_CHECK(sounds(f, node, 24, 750));
  AUD_CHECK(sounds(f, node, 127, 375));
  info = loadPreset(f, node, folder, via, "presets/down-open/main.sfz");
  AUD_CHECK(info.result == AUD_OK);
  f.renderOffline(256);
  AUD_CHECK(sounds(f, node, 0, shifted(750, -24)));
  AUD_CHECK(sounds(f, node, 11, shifted(750, -13)));
  AUD_CHECK(sounds(f, node, 24, 750));
  AUD_CHECK(sounds(f, node, 127, 375));
  if (aud_test::failures() > failuresBefore) std::printf("  loaded via %s\n", nameOf(via));
}

}  // namespace

// ############################################################################
// The layout

AUD_TEST(plays_presets_that_include_their_sample_set_from_files) {
  playsThePresets(Via::file);
}

AUD_TEST(plays_presets_that_include_their_sample_set_from_apk_assets) {
  playsThePresets(Via::apk);
}

AUD_TEST(plays_presets_that_include_their_sample_set_as_text) {
  playsThePresets(Via::text);
}

AUD_TEST(plays_presets_that_include_their_sample_set_as_text_of_apk_assets) {
  playsThePresets(Via::apkText);
}

AUD_TEST(plays_a_sample_set_alone_and_reads_the_includes_through_the_backend) {
  Fixture f;
  TempFolder folder;
  writeLayout(folder);
  ScopedApkAssets assets(folder.path);
  const int32_t node = f.sampler();
  // The set alone, without a shift: its samples resolve against its own
  // folder. Without common/level.sfz it plays at the full level.
  const int32_t id = f.loadFile(node, "apk:/samplesets/sines/instrument.sfz");
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(id).result == AUD_OK);
  f.renderOffline(256);
  const Played low = play(f, node, 12);
  AUD_CHECK_NEAR(low.frequency, 750.0, 1.0);
  AUD_CHECK_NEAR(low.level, 2 * kHalfLevel, 0.003);
  AUD_CHECK_NEAR(play(f, node, 115).frequency, 375.0, 1.0);
  // The backend read the root file and its include.
  const std::vector<std::string>& paths = assets.backend->openedPaths;
  AUD_CHECK(firstOf(paths, 2) == (std::vector<std::string>{"samplesets/sines/instrument.sfz",
                                                           "samplesets/sines/regions.sfz"}));
  // A preset reads the includes it names, nested ones relative to the root
  // file, then the samples.
  assets.backend->openedPaths.clear();
  AUD_CHECK(loadPreset(f, node, folder, Via::apk, "presets/up/main.sfz").result == AUD_OK);
  AUD_CHECK(firstOf(paths, 4) == (std::vector<std::string>{"presets/up/main.sfz",
                                                           "common/engine.sfz", "common/level.sfz",
                                                           "samplesets/sines/regions.sfz"}));
}

AUD_TEST(a_second_control_sets_the_default_path_back_to_none) {
  Fixture f;
  TempFolder folder;
  writeLayout(folder);
  ScopedApkAssets assets(folder.path);
  const int32_t node = f.sampler();
  // The <control> of common/engine.sfz follows the one with default_path:
  // the samples of the set resolve against the preset's own folder, where
  // they are not.
  for (Via via : {Via::file, Via::apk, Via::text, Via::apkText}) {
    const AudLoadInfo info = loadPreset(f, node, folder, via, "presets/reset/main.sfz");
    AUD_CHECK(info.result == AUD_ERROR_FAILED);
    AUD_CHECK(std::string(info.message) ==
              "sfizz could not read 2 samples: samples/low.wav, samples/high.flac");
  }
}

// ############################################################################
// Includes that are missing

AUD_TEST(fails_a_load_whose_include_is_missing_and_names_it) {
  Fixture f;
  TempFolder folder;
  writeLayout(folder);
  ScopedApkAssets assets(folder.path);
  const int32_t node = f.sampler();
  AUD_CHECK(loadPreset(f, node, folder, Via::file, "presets/up/main.sfz").result == AUD_OK);
  f.renderOffline(256);
  f.noteOn(node, 103);
  f.renderOffline(4800);
  // The set $SET=gone does not exist: the load fails and names the file
  // its #include resolved to, relative to the folder of the preset like a
  // sample, although common/ loaded. The load's name holds the preset.
  for (Via via : {Via::file, Via::apk, Via::text, Via::apkText}) {
    const AudLoadInfo info = loadPreset(f, node, folder, via, "presets/gone/main.sfz");
    AUD_CHECK(info.result == AUD_ERROR_FAILED);
    AUD_CHECK(std::string(info.message) ==
              "sfizz could not read 1 include: ../../samplesets/gone/regions.sfz");
    AUD_CHECK(std::string(info.name) == pathOf(folder, via, "presets/gone/main.sfz"));
  }
  // A missing include fails the load even beside regions that load, and
  // every missing one is named. (The folder of the virtual path exists: the
  // filesystem resolves the ".." of an include through it.)
  const int32_t text = f.loadText(node, folder.file("presets/up/text.sfz"),
                                  "<region> sample=*sine\n"
                                  "#include \"../../common/engine.sfz\"\n"
                                  "#include \"../../common/gone.sfz\"\n"
                                  "#include \"lost.sfz\"\n");
  AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
  AUD_CHECK(f.load(text).result == AUD_ERROR_FAILED);
  AUD_CHECK(std::string(f.load(text).message) ==
            "sfizz could not read 2 includes: ../../common/gone.sfz, lost.sfz");
  // The instrument that played plays on.
  f.renderOffline(4800);
  AUD_CHECK_NEAR(frequencyOf(f.left, since(f, 4800), f.left.size()), 375.0, 1.0);
  AUD_CHECK_NEAR(peak(f.left, since(f, 4800), f.left.size()), kHalfLevel, 0.003);
}

// ############################################################################
// FLAC

AUD_TEST(plays_a_flac_sample_like_the_wav_of_its_frames_with_its_loop) {
  // The same frames as FLAC with the loop in the SFZ file, as FLAC with the
  // loop in a smpl chunk of the file, and as WAV: the renders of a note
  // that outlasts the sample are the same, from files and from the assets.
  TempFolder folder;
  const std::vector<float> frames = sineFrames(375, kHighFrames);
  writeFlac(folder.file("opcodes.flac"), frames);
  writeFlac(folder.file("smpl.flac"), frames, 48000, kLoopStart, kLoopEnd);
  writeWav(folder.file("frames.wav"), quantized16(frames));
  writeText(folder.file("wav.sfz"),
            "<region> sample=frames.wav pitch_keycenter=69 " + kLoop + "\n");
  writeText(folder.file("opcodes.sfz"),
            "<region> sample=opcodes.flac pitch_keycenter=69 " + kLoop + "\n");
  writeText(folder.file("smpl.sfz"), "<region> sample=smpl.flac pitch_keycenter=69\n");
  ScopedApkAssets assets(folder.path);
  const auto render = [](const std::string& file, uint32_t note) {
    Fixture f;
    const int32_t node = f.sampler();
    const int32_t id = f.loadFile(node, file);
    AUD_CHECK(aud_graph_wait_loads(f.graph, node, 10000) == AUD_OK);
    AUD_CHECK(f.load(id).result == AUD_OK);
    AUD_CHECK(std::string(f.load(id).message).empty());
    f.renderOffline(256);
    f.noteOn(node, note);
    f.renderOffline(48000);
    return f.left;
  };
  for (uint32_t note : {69u, 76u}) {
    const std::vector<float> wav = render(folder.file("wav.sfz"), note);
    AUD_CHECK(peak(wav, wav.size() - 4800, wav.size()) > 0.03f);
    for (const std::string& file :
         {folder.file("opcodes.sfz"), folder.file("smpl.sfz"), std::string("apk:/opcodes.sfz"),
          std::string("apk:/smpl.sfz")}) {
      const std::vector<float> flac = render(file, note);
      AUD_CHECK(flac == wav);
      if (flac != wav) std::printf("  %s, note %u, differs from the WAV\n", file.c_str(), note);
    }
  }
}

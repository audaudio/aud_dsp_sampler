// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the voice manager of the sfizz fork (src/sfizz/VoiceManager.cpp)
// through a synth of its own, without the node (ticket 29): the count of
// the voices that sound, are released and render, the order in which a
// voice limit of the host steals them, and the release triggers a stolen
// voice does not start; that the walk over the busy voices of ticket 30
// picks and visits the voices the walk over all voices did. The instruments
// play sfizz's generators, so no file is read.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "AudioSpan.h"
#include "Synth.h"
#include "Voice.h"

#include "aud_test.hpp"

namespace {

constexpr int kBlock = 256;
constexpr int kFade = 480;

// A synth that plays `text`, and a block to render it into.
struct Played {
  sfz::Synth synth;
  std::vector<float> left = std::vector<float>(kBlock);
  std::vector<float> right = std::vector<float>(kBlock);

  explicit Played(const std::string& text) {
    synth.setSampleRate(48000);
    synth.setSamplesPerBlock(kBlock);
    AUD_CHECK(synth.loadSfzString("/aud_dsp_sampler/voices.sfz", text));
  }

  void render(int blocks = 1) {
    float* channels[2] = {left.data(), right.data()};
    for (int i = 0; i < blocks; ++i) synth.renderBlock(sfz::AudioSpan<float>(channels, 2, 0, kBlock));
  }

  // The voices that are not free: their note and whether they are stolen,
  // sorted.
  std::vector<std::pair<int, bool>> voices() const {
    std::vector<std::pair<int, bool>> result;
    for (const sfz::Voice* voice : synth.getActiveVoices()) {
      result.emplace_back(voice->getTriggerEvent().number, voice->isStolen());
    }
    std::sort(result.begin(), result.end());
    return result;
  }
};

bool operator==(const sfz::VoiceCount& a, const sfz::VoiceCount& b) {
  return a.sounding == b.sounding && a.released == b.released && a.rendered == b.rendered;
}

sfz::VoiceCount count(int sounding, int released, int rendered) {
  sfz::VoiceCount result;
  result.sounding = sounding;
  result.released = released;
  result.rendered = rendered;
  return result;
}

// Sines whose released voices sound for two seconds.
const char* const kSines = "<region> sample=*sine ampeg_release=2\n";

}  // namespace

// ############################################################################
// countVoices

AUD_TEST(sfizz_voice_manager_counts_sounding_released_and_rendered_voices) {
  Played played(kSines);
  AUD_CHECK(played.synth.countVoices() == count(0, 0, 0));
  for (int note : {60, 62, 64}) played.synth.noteOn(0, note, 100);
  played.render();
  AUD_CHECK(played.synth.countVoices() == count(3, 0, 3));
  // A note-off releases its voice, which sounds on in its release.
  played.synth.noteOff(0, 62, 0);
  played.render();
  AUD_CHECK(played.synth.countVoices() == count(3, 1, 3));
  // The pedal holds a note-off until it goes up.
  played.synth.cc(0, 64, 127);
  played.synth.noteOff(0, 64, 0);
  played.render();
  AUD_CHECK(played.synth.countVoices() == count(3, 1, 3));
  played.synth.cc(0, 64, 0);
  played.render();
  AUD_CHECK(played.synth.countVoices() == count(3, 2, 3));
}

AUD_TEST(sfizz_voice_manager_counts_voices_sfizz_turned_off_as_rendered_only) {
  // A region of one voice: the second note turns the first off, which
  // fades out and renders without sounding.
  Played played("<region> sample=*sine polyphony=1 ampeg_release=2\n");
  played.synth.noteOn(0, 60, 100);
  played.render();
  played.synth.noteOn(0, 62, 100);
  AUD_CHECK(played.synth.countVoices() == count(1, 0, 2));
}

// ############################################################################
// stealVoices

AUD_TEST(sfizz_voice_manager_steals_released_voices_first_and_the_oldest_first) {
  Played played(kSines);
  for (int note : {60, 62, 64, 66}) {
    played.synth.noteOn(0, note, 100);
    played.render(2);
  }
  played.synth.noteOff(0, 62, 0);
  played.synth.noteOff(0, 66, 0);
  played.render();
  // The older released voice, the younger one, then the oldest held one.
  const std::vector<int> order = {62, 66, 60, 64};
  for (size_t i = 0; i < order.size(); ++i) {
    const sfz::VoiceCount stolen = played.synth.stealVoices(1, kFade);
    AUD_CHECK(stolen.sounding == 1 && stolen.released == (i < 2 ? 1 : 0));
    for (const auto& voice : played.voices()) {
      if (voice.first == order[i]) AUD_CHECK(voice.second);
    }
    AUD_CHECK(played.synth.countVoices().sounding == static_cast<int>(order.size() - i - 1));
  }
  // Nothing is left to steal, and nothing is asked.
  AUD_CHECK(played.synth.stealVoices(1, kFade) == count(0, 0, 0));
  AUD_CHECK(played.synth.stealVoices(0, kFade) == count(0, 0, 0));
  // The fades end within two blocks.
  AUD_CHECK(played.synth.countVoices().rendered == 4);
  played.render(2);
  AUD_CHECK(played.synth.countVoices() == count(0, 0, 0));
}

AUD_TEST(sfizz_voice_manager_steals_at_least_the_voices_it_is_asked_for) {
  Played played(kSines);
  for (int note : {60, 62, 64, 66, 68}) played.synth.noteOn(0, note, 100);
  played.render();
  AUD_CHECK(played.synth.stealVoices(3, kFade).sounding == 3);
  AUD_CHECK(played.synth.countVoices() == count(2, 0, 5));
  AUD_CHECK(played.synth.stealVoices(10, kFade).sounding == 2);
  AUD_CHECK(played.synth.countVoices() == count(0, 0, 5));
}

AUD_TEST(sfizz_voice_manager_steals_the_layers_of_a_note_together) {
  // Two layers on every key: a note starts two sister voices.
  Played played("<region> sample=*sine ampeg_release=2\n<region> sample=*saw ampeg_release=2\n");
  played.synth.noteOn(0, 60, 100);
  played.render();
  played.synth.noteOn(0, 62, 100);
  played.render();
  AUD_CHECK(played.synth.countVoices() == count(4, 0, 4));
  // One voice is asked for, the two of the older note go.
  AUD_CHECK(played.synth.stealVoices(1, kFade) == count(2, 0, 0));
  AUD_CHECK(played.voices() ==
            (std::vector<std::pair<int, bool>>{{60, true}, {60, true}, {62, false}, {62, false}}));
}

AUD_TEST(sfizz_voice_manager_steals_a_layer_sfizz_turned_off_with_its_sisters) {
  // The first layer of key 60 is turned off by the group of key 62, the
  // second layer sounds on. Stolen, the note takes the turned off layer
  // along without counting it, since it no longer sounds.
  Played played(
      "<region> sample=*sine key=60 group=1 off_by=2 ampeg_release=2\n"
      "<region> sample=*saw key=60 group=3 ampeg_release=2\n"
      "<region> sample=*triangle key=62 group=2 ampeg_release=2\n");
  played.synth.noteOn(0, 60, 100);
  played.render();
  played.synth.noteOn(0, 62, 100);
  AUD_CHECK(played.synth.countVoices() == count(2, 0, 3));
  AUD_CHECK(played.synth.stealVoices(1, kFade) == count(1, 0, 0));
  AUD_CHECK(played.voices() ==
            (std::vector<std::pair<int, bool>>{{60, true}, {60, true}, {62, false}}));
}

// ############################################################################
// Release triggers

AUD_TEST(sfizz_voice_manager_starts_no_release_trigger_after_a_stolen_voice) {
  // A release trigger follows a note whose attack voice plays: not one
  // whose voice was stolen, also while it fades.
  const std::string text =
      "<region> sample=*sine key=60 ampeg_release=2\n"
      "<region> sample=*saw key=60 trigger=release\n";
  for (const bool steal : {false, true}) {
    Played played(text);
    played.synth.noteOn(0, 60, 100);
    played.render();
    if (steal) AUD_CHECK(played.synth.stealVoices(1, kFade).sounding == 1);
    played.synth.noteOff(10, 60, 0);
    AUD_CHECK(played.synth.countVoices().rendered == (steal ? 1 : 2));
  }
}

// ############################################################################
// The walk over the busy voices (ticket 30)

AUD_TEST(sfizz_voice_manager_starts_a_note_on_the_first_free_voice) {
  // A voice that ended frees its place in the list; the next note takes
  // the first free voice, as the walk over the whole list did.
  Played played("<region> sample=*sine ampeg_release=0.001\n");
  for (int note : {60, 62, 64}) played.synth.noteOn(0, note, 100);
  played.render();
  played.synth.noteOff(0, 62, 0);
  played.render(30);
  AUD_CHECK(played.synth.getVoiceView(1)->isFree());
  played.synth.noteOn(0, 66, 100);
  const std::vector<int> notes = {60, 66, 64};
  for (int i = 0; i < 3; ++i) {
    AUD_CHECK(!played.synth.getVoiceView(i)->isFree());
    AUD_CHECK(played.synth.getVoiceView(i)->getTriggerEvent().number == notes[i]);
  }
  AUD_CHECK(played.synth.getVoiceView(3)->isFree());
}

AUD_TEST(sfizz_voice_manager_visits_a_release_trigger_started_by_an_off_group) {
  // Key 62 turns the voice of key 60 off, whose release trigger starts in
  // the walk over the voices, behind the voice turned off; the walk visits
  // it, and it stays. Then the two layers of key 62 start.
  Played played(
      "<region> sample=*sine key=60 group=1 off_by=2 ampeg_release=2\n"
      "<region> sample=*saw key=60 trigger=release ampeg_release=2\n"
      "<region> sample=*triangle key=62 group=2 ampeg_release=2\n"
      "<region> sample=*square key=62 group=3 off_by=2 ampeg_release=2\n");
  played.synth.noteOn(0, 60, 100);
  played.render();
  played.synth.noteOn(5, 62, 100);
  AUD_CHECK(played.synth.countVoices() == count(3, 0, 4));
  const sfz::Voice* turnedOff = played.synth.getVoiceView(0);
  const sfz::Voice* release = played.synth.getVoiceView(1);
  AUD_CHECK(turnedOff->offedOrFree() && !turnedOff->isFree());
  AUD_CHECK(release->getTriggerEvent().type == sfz::TriggerEventType::NoteOff);
  AUD_CHECK(release->getTriggerEvent().number == 60 && !release->offedOrFree());
  for (int i : {2, 3}) AUD_CHECK(played.synth.getVoiceView(i)->getTriggerEvent().number == 62);
}

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The entry point of aud_dsp_sampler (step S9, ticket 25): registers the SFZ
// sampler node on the package's fork of sfizz with an engine through the C
// ABI of aud_audio_core (ABI 0.4).
//
// The node renders on the audio thread with sfizz's realtime functions only,
// without allocating. A string setting starts a load on a worker thread of
// the engine and returns its id: the load fills a standby synth while the
// node plays the old one, and the audio thread adopts the new synth at a
// block start with a 5 ms crossfade and gives it the controllers, the pitch
// bend, the pressure and the program of the old one. A newer load cancels
// one that runs; a load that fails keeps the instrument that plays. A load
// fails when sfizz cannot parse the instrument (the message names the first
// error), when none of its samples can be read, or with AUD_ERROR_CAPACITY
// when the heads of its samples alone exceed the sample cache budget. A load
// that ends well names the samples it could not read and the effect types
// sfizz does not know, which pass the signal through. On Android a path
// "apk:/<path>" names an asset of the APK (ticket 27), read in place; the
// includes and samples of such an SFZ file resolve inside the APK too.
//
// The samples live in one store per process that every node of every engine
// shares: the head of every sample (8192 frames), pinned while an instrument
// uses it, and the whole file, streamed by a task of the engine when a voice
// starts and kept while a voice reads it. The engine's sample cache budget
// trims the rest, the least recently used first. A voice that outruns its
// stream plays silence, keeps its position and is reported as
// AUD_ERROR_UNDERFLOW.
//
// Messages (osc-001) reach sfizz's message API; the answers come back through
// the engine. A message with a string or a blob argument, which sfizz would
// copy or parse on the audio thread, is refused and reported as
// AUD_ERROR_UNSUPPORTED, like the per-note messages of MIDI 2.0 except poly
// pressure.

#ifndef AUD_DSP_SAMPLER_H
#define AUD_DSP_SAMPLER_H

#include <stdint.h>

#include "aud_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// The type id of the sampler node.
#define AUD_DSP_SAMPLER_SFZ_TYPE_ID "aud.sampler.sfz"

// The parameters of the sampler node, by index; their ids are stable.
enum {
  // "volume": the output gain in dB, -60 to +6, ramped.
  AUD_SFZ_PARAM_VOLUME = 0,
  // "polyphony": the voices of the node, 1 to 256. A change loads the
  // instrument again on a worker thread, since sfizz sets its voices only
  // while it does not render; the notes that sound fade out.
  AUD_SFZ_PARAM_POLYPHONY = 1,
  // "sample_quality": sfizz's interpolation quality while it renders live,
  // 0 to 10; an offline render uses sfizz's best.
  AUD_SFZ_PARAM_SAMPLE_QUALITY = 2,
  // "oscillator_quality": the quality of sfizz's oscillators while it
  // renders live, 0 to 3.
  AUD_SFZ_PARAM_OSCILLATOR_QUALITY = 3,
  // "tuning_frequency": the frequency of A4 in Hz, 400 to 480. Like the
  // polyphony, a change loads the instrument again: sfizz's tuning tables
  // allocate.
  AUD_SFZ_PARAM_TUNING_FREQUENCY = 4,
  // "stretch_tuning": the piano stretch of sfizz, 0 to 1; a change loads the
  // instrument again.
  AUD_SFZ_PARAM_STRETCH_TUNING = 5,
  AUD_SFZ_NUM_PARAMS = 6,
};

// The string settings of the sampler node (AudNodeVTable.set_string). Each
// one that loads returns the id of its load (> 0); a newer load cancels a
// pending one, a failed load keeps the instrument that plays.
enum {
  // "sfz_file": loads an SFZ file; the value is its path.
  AUD_SFZ_KEY_FILE = 0,
  // "sfz_virtual_path": the path the samples of the next SFZ text resolve
  // against; it starts no load of its own and returns AUD_OK.
  AUD_SFZ_KEY_VIRTUAL_PATH = 1,
  // "sfz_text": loads SFZ text relative to the virtual path; an empty text
  // or file unloads the instrument.
  AUD_SFZ_KEY_TEXT = 2,
  // "scala_file": loads a Scala tuning file with the instrument; without an
  // instrument it waits for one and returns AUD_OK.
  AUD_SFZ_KEY_SCALA_FILE = 3,
  // "scala_text": loads a Scala tuning given as text with the instrument,
  // like scala_file.
  AUD_SFZ_KEY_SCALA_TEXT = 4,
};

// [control] Registers the node types with the host api of an engine; AUD_OK
// or the first error code. An AudRegisterFunction.
AUD_EXPORT int32_t aud_dsp_sampler_register(const AudHostApi* host);

// [any thread] The bytes the sample store of the process holds.
AUD_EXPORT int64_t aud_dsp_sampler_store_bytes(void);

// [any thread] The sample files the store of the process keeps.
AUD_EXPORT int32_t aud_dsp_sampler_store_files(void);

// [control] Gives the sampler the assets of an Android app, an AAssetManager*:
// a path "apk:/<path>" in sfz_file, scala_file or sfz_virtual_path, and
// inside an SFZ file, then names an asset of the APK, read in place. The
// Android plugin of the package calls it when the app process starts; an
// app with its own JNI may call it instead. AUD_OK, AUD_ERROR_INVALID_ARGUMENT
// for NULL, AUD_ERROR_UNSUPPORTED off Android, where a load of an apk:/ path
// ends with AUD_ERROR_UNSUPPORTED.
AUD_EXPORT int32_t aud_dsp_sampler_set_android_assets(void* asset_manager);

#ifdef __cplusplus
}
#endif

#endif  // AUD_DSP_SAMPLER_H

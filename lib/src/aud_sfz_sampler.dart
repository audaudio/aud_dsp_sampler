// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// The SFZ sampler node `aud.sampler.sfz` on the package's fork of sfizz:
/// the ids of its parameters, string settings and event input, by which an
/// engine addresses them (`graph.setParam(node, AudSfzSampler.volume, -6)`).
///
/// A string setting starts a load on a worker thread of the engine and
/// returns its id; the node plays the old instrument until the new one is
/// loaded and swaps them with a crossfade of 5 ms. [sfzVirtualPath] starts
/// no load: it names the path the samples of the next [sfzText] resolve
/// against. An empty [sfzText] or [sfzFile] unloads the instrument, as
/// does one without regions, e.g. a text with only a comment.
///
/// Messages to the node reach sfizz's message API (osc-001):
/// `/graph/<g>/node/<n>/msg/region0/ampeg_attack f 0.1` becomes sfizz's
/// `/region0/ampeg_attack`. A message with a string or a blob argument is
/// refused, since sfizz would copy or parse it on the audio thread.
abstract final class AudSfzSampler {
  /// The type id of the node.
  static const String typeId = 'aud.sampler.sfz';

  /// The version of the node type.
  static const int version = 1;

  // ...........................................................................
  // Parameters

  /// The output gain in dB, -60 to +6, ramped; default 0.
  static const String volume = 'volume';

  /// The voices of the node, 1 to 256; default 64. A change loads the
  /// instrument again on a worker thread, and the notes that sound fade out.
  static const String polyphony = 'polyphony';

  /// sfizz's interpolation quality while it renders live, 0 to 10;
  /// default 2. An offline render uses sfizz's best.
  static const String sampleQuality = 'sample_quality';

  /// The quality of sfizz's oscillators while it renders live, 0 to 3;
  /// default 1.
  static const String oscillatorQuality = 'oscillator_quality';

  /// The frequency of A4 in Hz, 400 to 480; default 440. Like [polyphony],
  /// a change loads the instrument again.
  static const String tuningFrequency = 'tuning_frequency';

  /// The piano stretch of sfizz, 0 to 1; default 0. A change loads the
  /// instrument again.
  static const String stretchTuning = 'stretch_tuning';

  /// The parameter ids in the order of their indices.
  static const List<String> params = [
    volume,
    polyphony,
    sampleQuality,
    oscillatorQuality,
    tuningFrequency,
    stretchTuning,
  ];

  // ...........................................................................
  // String settings

  /// Loads an SFZ file; the value is its path, or `asset:<id>` in a graph
  /// document.
  static const String sfzFile = 'sfz_file';

  /// The path the samples of the next [sfzText] resolve against.
  static const String sfzVirtualPath = 'sfz_virtual_path';

  /// Loads SFZ text relative to [sfzVirtualPath].
  static const String sfzText = 'sfz_text';

  /// Loads a Scala tuning file with the instrument.
  static const String scalaFile = 'scala_file';

  /// Loads a Scala tuning given as text with the instrument.
  static const String scalaText = 'scala_text';

  /// The string ids in the order of their keys, which is also the order in
  /// which a preset applies them.
  static const List<String> strings = [
    sfzFile,
    sfzVirtualPath,
    sfzText,
    scalaFile,
    scalaText,
  ];

  // ...........................................................................
  // Events

  /// The id of the event input: UMP of MIDI 1.0 and 2.0, numeric controls
  /// as sfizz's CCs by number, and messages.
  static const String midi = 'midi';
}

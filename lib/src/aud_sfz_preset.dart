// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';

import 'aud_sfz_sampler.dart';

// #############################################################################
/// Builds node presets of [AudSfzSampler] (`aud_node_preset.schema.json` of
/// the core): an SFZ instrument is a preset of the node, which travels in
/// graph documents, plugin state and files.
///
/// ```dart
/// final piano = AudSfzPreset.file('asset:piano', name: 'Piano', polyphony: 32);
/// graph.createNode(AudSfzSampler.typeId, preset: piano);
/// ```
abstract final class AudSfzPreset {
  /// A preset that loads the SFZ [file]: a path, or `asset:<id>` in a graph
  /// document whose assets name the file. A parameter left out keeps the
  /// default of the node; giving both [scalaFile] and [scalaText] throws an
  /// [ArgumentError].
  ///
  /// - [file] the path of the SFZ file, or `asset:<id>`
  /// - [name] the name of the preset
  /// - [volume] the output gain in dB, -60 to +6 ([AudSfzSampler.volume])
  /// - [polyphony] the voices, 1 to 256 ([AudSfzSampler.polyphony])
  /// - [sampleQuality] sfizz's interpolation quality while it renders live,
  ///   0 to 10 ([AudSfzSampler.sampleQuality])
  /// - [oscillatorQuality] the quality of sfizz's oscillators while it
  ///   renders live, 0 to 3 ([AudSfzSampler.oscillatorQuality])
  /// - [tuningFrequency] the frequency of A4 in Hz, 400 to 480
  ///   ([AudSfzSampler.tuningFrequency])
  /// - [stretchTuning] the piano stretch, 0 to 1
  ///   ([AudSfzSampler.stretchTuning])
  /// - [scalaFile] a Scala tuning file that loads with the instrument: a
  ///   path, or `asset:<id>`
  /// - [scalaText] a Scala tuning given as text, instead of [scalaFile]
  static AudNodePreset file(
    String file, {
    String name = '',
    double? volume,
    int? polyphony,
    int? sampleQuality,
    int? oscillatorQuality,
    double? tuningFrequency,
    double? stretchTuning,
    String? scalaFile,
    String? scalaText,
  }) => _preset(
    name: name,
    strings: {AudSfzSampler.sfzFile: file},
    volume: volume,
    polyphony: polyphony,
    sampleQuality: sampleQuality,
    oscillatorQuality: oscillatorQuality,
    tuningFrequency: tuningFrequency,
    stretchTuning: stretchTuning,
    scalaFile: scalaFile,
    scalaText: scalaText,
  );

  /// A preset that loads the SFZ [text] whose samples resolve against
  /// [virtualPath], e.g. the path the text would have as a file. A parameter
  /// left out keeps the default of the node; giving both [scalaFile] and
  /// [scalaText] throws an [ArgumentError].
  ///
  /// - [text] the SFZ text of the instrument
  /// - [virtualPath] the path the samples of [text] resolve against
  /// - [name] the name of the preset
  /// - [volume] the output gain in dB, -60 to +6 ([AudSfzSampler.volume])
  /// - [polyphony] the voices, 1 to 256 ([AudSfzSampler.polyphony])
  /// - [sampleQuality] sfizz's interpolation quality while it renders live,
  ///   0 to 10 ([AudSfzSampler.sampleQuality])
  /// - [oscillatorQuality] the quality of sfizz's oscillators while it
  ///   renders live, 0 to 3 ([AudSfzSampler.oscillatorQuality])
  /// - [tuningFrequency] the frequency of A4 in Hz, 400 to 480
  ///   ([AudSfzSampler.tuningFrequency])
  /// - [stretchTuning] the piano stretch, 0 to 1
  ///   ([AudSfzSampler.stretchTuning])
  /// - [scalaFile] a Scala tuning file that loads with the instrument: a
  ///   path, or `asset:<id>`
  /// - [scalaText] a Scala tuning given as text, instead of [scalaFile]
  static AudNodePreset text(
    String text, {
    required String virtualPath,
    String name = '',
    double? volume,
    int? polyphony,
    int? sampleQuality,
    int? oscillatorQuality,
    double? tuningFrequency,
    double? stretchTuning,
    String? scalaFile,
    String? scalaText,
  }) => _preset(
    name: name,
    strings: {
      AudSfzSampler.sfzVirtualPath: virtualPath,
      AudSfzSampler.sfzText: text,
    },
    volume: volume,
    polyphony: polyphony,
    sampleQuality: sampleQuality,
    oscillatorQuality: oscillatorQuality,
    tuningFrequency: tuningFrequency,
    stretchTuning: stretchTuning,
    scalaFile: scalaFile,
    scalaText: scalaText,
  );

  // ...........................................................................
  static AudNodePreset _preset({
    required String name,
    required Map<String, String> strings,
    required double? volume,
    required int? polyphony,
    required int? sampleQuality,
    required int? oscillatorQuality,
    required double? tuningFrequency,
    required double? stretchTuning,
    required String? scalaFile,
    required String? scalaText,
  }) {
    if (scalaFile != null && scalaText != null) {
      throw ArgumentError('Give a Scala file or a Scala text, not both');
    }
    return AudNodePreset(
      typeId: AudSfzSampler.typeId,
      name: name,
      nodeVersion: AudSfzSampler.version,
      params: {
        AudSfzSampler.volume: ?volume,
        AudSfzSampler.polyphony: ?polyphony?.toDouble(),
        AudSfzSampler.sampleQuality: ?sampleQuality?.toDouble(),
        AudSfzSampler.oscillatorQuality: ?oscillatorQuality?.toDouble(),
        AudSfzSampler.tuningFrequency: ?tuningFrequency,
        AudSfzSampler.stretchTuning: ?stretchTuning,
      },
      strings: {
        ...strings,
        AudSfzSampler.scalaFile: ?scalaFile,
        AudSfzSampler.scalaText: ?scalaText,
      },
    );
  }
}

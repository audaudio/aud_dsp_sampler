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
  /// document whose assets name the file.
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
  /// [virtualPath], e.g. the path the text would have as a file.
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

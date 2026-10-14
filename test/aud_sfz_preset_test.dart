// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:test/test.dart';

void main() {
  group('AudSfzPreset', () {
    test('loads a file with parameters', () {
      final preset = AudSfzPreset.file(
        'asset:piano',
        name: 'Piano',
        volume: -6,
        polyphony: 32,
        sampleQuality: 3,
        oscillatorQuality: 2,
        tuningFrequency: 442,
        stretchTuning: 0.5,
        maxVoices: 24,
        scalaFile: 'asset:wide',
      );
      expect(preset.typeId, AudSfzSampler.typeId);
      expect(preset.name, 'Piano');
      expect(preset.nodeVersion, AudSfzSampler.version);
      expect(preset.params, {
        AudSfzSampler.volume: -6.0,
        AudSfzSampler.polyphony: 32.0,
        AudSfzSampler.sampleQuality: 3.0,
        AudSfzSampler.oscillatorQuality: 2.0,
        AudSfzSampler.tuningFrequency: 442.0,
        AudSfzSampler.stretchTuning: 0.5,
        AudSfzSampler.maxVoices: 24.0,
      });
      expect(preset.strings, {
        AudSfzSampler.sfzFile: 'asset:piano',
        AudSfzSampler.scalaFile: 'asset:wide',
      });
    });

    test('loads a text against its virtual path', () {
      final preset = AudSfzPreset.text(
        '<region> sample=a.wav',
        virtualPath: '/instruments/a.sfz',
        scalaText: '! wide.scl',
      );
      expect(preset.params, isEmpty);
      expect(preset.strings.keys, [
        AudSfzSampler.sfzVirtualPath,
        AudSfzSampler.sfzText,
        AudSfzSampler.scalaText,
      ]);
      expect(preset.strings[AudSfzSampler.sfzText], '<region> sample=a.wav');
    });

    test('takes one tuning', () {
      expect(
        () => AudSfzPreset.file('a.sfz', scalaFile: 'a.scl', scalaText: 'x'),
        throwsA(
          isA<ArgumentError>().having(
            (e) => e.message,
            'message',
            'Give a Scala file or a Scala text, not both',
          ),
        ),
      );
    });

    test('fits the node and round trips through JSON', () {
      final graph = AudGraphFfi(listen: false, maxFrames: 256);
      addTearDown(graph.dispose);
      AudDspSampler.registerWith(graph.hostApi);
      final descriptor = graph.nodeType(AudSfzSampler.typeId)!;
      final preset = AudSfzPreset.text(
        '<region> sample=a.wav',
        virtualPath: '/a.sfz',
        name: 'A',
        polyphony: 8,
        maxVoices: 6,
      );
      expect(preset.validate(descriptor), isEmpty);
      final copy = AudNodePreset.fromJson(preset.toJson());
      expect(copy.toJson(), preset.toJson());
    });
  });
}

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/src/aud_dsp_sampler_bindings_generated.dart'
    as bindings;
import 'package:test/test.dart';

void main() {
  group('AudSfzSampler', () {
    test('names what the native node declares', () {
      final graph = AudGraphFfi(listen: false, maxFrames: 256);
      addTearDown(graph.dispose);
      AudDspSampler.registerWith(graph.hostApi);
      final descriptor = graph.nodeType(AudSfzSampler.typeId)!;
      expect(descriptor.version, AudSfzSampler.version);
      expect(descriptor.params.map((p) => p.id), AudSfzSampler.params);
      expect(descriptor.stringKeys.map((k) => k.id), AudSfzSampler.strings);
      expect(descriptor.stringKeys.map((k) => k.key), [0, 1, 2, 3, 4]);
      expect(descriptor.eventInputs.single.id, AudSfzSampler.midi);
      expect(descriptor.capabilities.strings, isTrue);
      expect(descriptor.capabilities.tail, isTrue);
      expect(descriptor.capabilities.messages, isTrue);
      expect(descriptor.outputBuses.single.defaultChannels, 2);
    });

    test('follows the indices of the C header', () {
      expect(AudSfzSampler.typeId, bindings.AUD_DSP_SAMPLER_SFZ_TYPE_ID);
      expect(AudSfzSampler.params, hasLength(bindings.AUD_SFZ_NUM_PARAMS));
      expect(
        {for (final (index, id) in AudSfzSampler.params.indexed) id: index},
        {
          AudSfzSampler.volume: bindings.AUD_SFZ_PARAM_VOLUME,
          AudSfzSampler.polyphony: bindings.AUD_SFZ_PARAM_POLYPHONY,
          AudSfzSampler.sampleQuality: bindings.AUD_SFZ_PARAM_SAMPLE_QUALITY,
          AudSfzSampler.oscillatorQuality:
              bindings.AUD_SFZ_PARAM_OSCILLATOR_QUALITY,
          AudSfzSampler.tuningFrequency:
              bindings.AUD_SFZ_PARAM_TUNING_FREQUENCY,
          AudSfzSampler.stretchTuning: bindings.AUD_SFZ_PARAM_STRETCH_TUNING,
        },
      );
      expect(
        {for (final (index, id) in AudSfzSampler.strings.indexed) id: index},
        {
          AudSfzSampler.sfzFile: bindings.AUD_SFZ_KEY_FILE,
          AudSfzSampler.sfzVirtualPath: bindings.AUD_SFZ_KEY_VIRTUAL_PATH,
          AudSfzSampler.sfzText: bindings.AUD_SFZ_KEY_TEXT,
          AudSfzSampler.scalaFile: bindings.AUD_SFZ_KEY_SCALA_FILE,
          AudSfzSampler.scalaText: bindings.AUD_SFZ_KEY_SCALA_TEXT,
        },
      );
    });
  });
}

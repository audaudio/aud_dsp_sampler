// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/src/aud_dsp_sampler_bindings_generated.dart'
    as bindings;
import 'package:test/test.dart';

import 'support/sampler_graph.dart';

void main() {
  group('AudSfzSampler', () {
    test('names what the native node declares', () {
      final graph = samplerGraph();
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
      final params = AudSfzSampler.params;
      expect(
        params.indexOf(AudSfzSampler.volume),
        bindings.AUD_SFZ_PARAM_VOLUME,
      );
      expect(
        params.indexOf(AudSfzSampler.polyphony),
        bindings.AUD_SFZ_PARAM_POLYPHONY,
      );
      expect(
        params.indexOf(AudSfzSampler.sampleQuality),
        bindings.AUD_SFZ_PARAM_SAMPLE_QUALITY,
      );
      expect(
        params.indexOf(AudSfzSampler.oscillatorQuality),
        bindings.AUD_SFZ_PARAM_OSCILLATOR_QUALITY,
      );
      expect(
        params.indexOf(AudSfzSampler.tuningFrequency),
        bindings.AUD_SFZ_PARAM_TUNING_FREQUENCY,
      );
      expect(
        params.indexOf(AudSfzSampler.stretchTuning),
        bindings.AUD_SFZ_PARAM_STRETCH_TUNING,
      );
      final strings = AudSfzSampler.strings;
      expect(strings.indexOf(AudSfzSampler.sfzFile), bindings.AUD_SFZ_KEY_FILE);
      expect(
        strings.indexOf(AudSfzSampler.sfzVirtualPath),
        bindings.AUD_SFZ_KEY_VIRTUAL_PATH,
      );
      expect(strings.indexOf(AudSfzSampler.sfzText), bindings.AUD_SFZ_KEY_TEXT);
      expect(
        strings.indexOf(AudSfzSampler.scalaFile),
        bindings.AUD_SFZ_KEY_SCALA_FILE,
      );
      expect(
        strings.indexOf(AudSfzSampler.scalaText),
        bindings.AUD_SFZ_KEY_SCALA_TEXT,
      );
    });
  });
}

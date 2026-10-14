// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_dsp_sampler/src/aud_dsp_sampler_bindings_generated.dart'
    as bindings;
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

void main() {
  group('aud_dsp_sampler_bindings_generated.dart', () {
    test('aud_dsp_sampler_register refuses a null host', () {
      expect(
        bindings.aud_dsp_sampler_register(nullptr),
        AUD_ERROR_INVALID_ARGUMENT,
      );
    });

    test('aud_dsp_sampler_voice_stats refuses null and a short struct', () {
      expect(
        bindings.aud_dsp_sampler_voice_stats(nullptr),
        AUD_ERROR_INVALID_ARGUMENT,
      );
      final stats = calloc<bindings.AudSfzVoiceStats>();
      addTearDown(() => calloc.free(stats));
      stats.ref.struct_size = sizeOf<bindings.AudSfzVoiceStats>() - 8;
      expect(
        bindings.aud_dsp_sampler_voice_stats(stats),
        AUD_ERROR_INVALID_ARGUMENT,
      );
      // The struct of the C header: 24 bytes of 32-bit fields, then the two
      // counters of 64 bits.
      expect(sizeOf<bindings.AudSfzVoiceStats>(), 40);
      stats.ref.struct_size = 40;
      expect(bindings.aud_dsp_sampler_voice_stats(stats), AUD_OK);
      expect(stats.ref.struct_size, 40);
    });
  });
}

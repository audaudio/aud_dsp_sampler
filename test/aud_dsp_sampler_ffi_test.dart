// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:test/test.dart';

import 'support/sampler_graph.dart';

void main() {
  group('AudDspSampler', () {
    test('registers with an engine once', () {
      final graph = samplerGraph();
      expect(graph.nodeType(AudSfzSampler.typeId), isNotNull);
      expect(
        () => AudDspSampler.registerWith(graph.hostApi),
        throwsA(
          isA<AudDspSamplerException>()
              .having((e) => e.code, 'code', AUD_ERROR_DUPLICATE_TYPE)
              .having(
                (e) => e.toString(),
                'text',
                'AudDspSamplerException: registration failed with '
                    'AUD_ERROR_DUPLICATE_TYPE',
              ),
        ),
      );
    });

    test('hands its register function to an engine', () {
      final graph = AudGraphFfi(listen: false, maxFrames: 256);
      addTearDown(graph.dispose);
      expect(AudDspSampler.name, 'aud_dsp_sampler');
      final register = AudDspSampler.register
          .asFunction<int Function(Pointer<core.AudHostApi>)>();
      expect(register(graph.hostApi), AUD_OK);
      expect(graph.nodeType(AudSfzSampler.typeId), isNotNull);
      expect(register(nullptr), AUD_ERROR_INVALID_ARGUMENT);
    });

    test('counts the store of the process', () {
      final directory = tempDirectory();
      final instrument = AudSfzTestInstrument.write(directory.path);
      final before = (AudDspSampler.storeBytes, AudDspSampler.storeFiles);
      final graph = samplerGraph();
      samplerNode(graph, instrument.preset());
      // Five mono heads of 8192 frames of 4 bytes.
      expect(AudDspSampler.storeFiles, before.$2 + 5);
      expect(AudDspSampler.storeBytes, before.$1 + 5 * 8192 * 4);
      expect(graph.cacheUsage.bytes, greaterThanOrEqualTo(5 * 8192 * 4));
    });
  });
}

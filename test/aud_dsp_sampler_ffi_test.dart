// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';
import 'dart:io';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:test/test.dart';

void main() {
  // A graph of the engine with the sampler registered, disposed at the end
  // of the test. The notifications wait for a pump.
  AudGraphFfi samplerGraph() {
    final graph = AudGraphFfi(listen: false, maxFrames: 256);
    addTearDown(graph.dispose);
    AudDspSampler.registerWith(graph.hostApi);
    return graph;
  }

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
      final directory = Directory.systemTemp.createTempSync('aud_dsp_sampler_');
      addTearDown(() => directory.deleteSync(recursive: true));
      final instrument = AudSfzTestInstrument.write(directory.path);
      final before = (AudDspSampler.storeBytes, AudDspSampler.storeFiles);
      final graph = samplerGraph();
      final node = graph.createNode(
        AudSfzSampler.typeId,
        preset: instrument.preset(),
      );
      graph.transaction((tx) => tx.connect(node, graph.io));
      graph.start();
      expect(
        graph.waitLoads(node: node, timeout: const Duration(seconds: 10)),
        isTrue,
      );
      // Five mono heads of 8192 frames of 4 bytes, and what other suites
      // load meanwhile: the suites of `dart test` run as isolates of one
      // process and share its store. The native test
      // shares_samples_between_nodes_and_graphs counts it exactly.
      expect(AudDspSampler.storeFiles, greaterThanOrEqualTo(before.$2 + 5));
      expect(
        AudDspSampler.storeBytes,
        greaterThanOrEqualTo(before.$1 + 5 * 8192 * 4),
      );
      expect(graph.cacheUsage.bytes, greaterThanOrEqualTo(5 * 8192 * 4));
    });
  });
}

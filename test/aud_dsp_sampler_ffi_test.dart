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

    test('loads flex envelopes on several nodes at once', () {
      // The nodes of an app that starts load at the same time on the workers
      // of the engine. Their flex envelopes share the curves of their shapes
      // in the process, whose map parallel loads corrupted (ticket 28).
      final graph = samplerGraph();
      final nodes = [
        for (var i = 0; i < 4; i++) graph.createNode(AudSfzSampler.typeId),
      ];
      graph.transaction((tx) {
        for (final node in nodes) {
          tx.connect(node, graph.io);
        }
      });
      graph.start();
      final ids = <int>[];
      for (var round = 0; round < 10; round++) {
        for (final (index, node) in nodes.indexed) {
          final shape = 0.5 + index + round / 32;
          ids.add(
            graph.setString(
              node,
              AudSfzSampler.sfzText,
              '<region> sample=*sine eg1_ampeg=1 eg1_sustain=2 '
              'eg1_time1=0.01 eg1_level1=1 eg1_shape1=$shape '
              'eg1_time2=0.02 eg1_level2=0.5 eg1_shape2=-2 '
              'eg1_time3=0.2 eg1_level3=0',
            ),
          );
        }
        expect(graph.waitLoads(timeout: const Duration(seconds: 10)), isTrue);
      }
      expect([
        for (final id in ids) graph.loadOf(id)?.result,
      ], List.filled(ids.length, AUD_OK));
    });
  });
}

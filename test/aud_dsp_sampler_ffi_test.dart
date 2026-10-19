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
import 'package:aud_midi_standard/aud_midi_standard.dart';
import 'package:ffi/ffi.dart';
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

  // Renders [blocks] blocks of 256 frames through the render entry of an
  // audio stream: a live render, whose nodes publish their voices to the
  // budget of the process.
  void renderLive(AudGraphFfi graph, {int blocks = 1}) {
    const frames = 256;
    using((arena) {
      final channels = arena<Pointer<Float>>(2);
      channels[0] = arena<Float>(frames);
      channels[1] = arena<Float>(frames);
      final bus = arena<core.AudAudioBus>();
      bus.ref
        ..struct_size = sizeOf<core.AudAudioBus>()
        ..num_channels = 2
        ..channels = channels;
      final request = arena<core.AudRenderRequest>();
      request.ref
        ..struct_size = sizeOf<core.AudRenderRequest>()
        ..frames = frames
        ..num_output_buses = 1
        ..outputs = bus;
      for (var i = 0; i < blocks; i++) {
        expect(aud_graph_render(graph.pointer.cast(), request), AUD_OK);
      }
    });
  }

  // Sends the note on or off of [note] to [node] with the next block.
  void play(AudGraph graph, AudNode node, MidiMessage message) {
    for (final event in AudUmpEvent.fromMessage(message)) {
      graph.sendEvent(node, event);
    }
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

    test('fails a load whose include is missing and names it', () {
      // A preset that includes the regions of a sample set that is not
      // there (ticket 31): the load fails, although a region of its own
      // loads, and names the include relative to the preset's folder.
      final directory = Directory.systemTemp.createTempSync('aud_dsp_sampler_');
      addTearDown(() => directory.deleteSync(recursive: true));
      final graph = samplerGraph();
      final node = graph.createNode(AudSfzSampler.typeId);
      graph.transaction((tx) => tx.connect(node, graph.io));
      graph.start();
      graph.setString(
        node,
        AudSfzSampler.sfzVirtualPath,
        '${directory.path}/preset.sfz',
      );
      final id = graph.setString(
        node,
        AudSfzSampler.sfzText,
        '#define \$SET gone\n'
        '<region> sample=*sine\n'
        '#include "samplesets/\$SET/regions.sfz"\n',
      );
      expect(
        graph.waitLoads(node: node, timeout: const Duration(seconds: 10)),
        isTrue,
      );
      final load = graph.loadOf(id)!;
      expect(
        (load.result, load.message),
        (
          AUD_ERROR_FAILED,
          'sfizz could not read 1 include: samplesets/gone/regions.sfz',
        ),
      );
    });

    group('the voices of the process', () {
      // The budget and the reserve belong to the process: each test leaves
      // them as it found them. No other suite renders live, so the voices
      // the stats count are those of the test.
      setUp(() {
        final budget = AudDspSampler.voiceBudget;
        final reserve = AudDspSampler.voiceReserve;
        addTearDown(() {
          AudDspSampler.voiceBudget = budget;
          AudDspSampler.voiceReserve = reserve;
        });
      });

      test('voiceBudget, 0 and below for none', () {
        AudDspSampler.voiceBudget = 24;
        expect(AudDspSampler.voiceBudget, 24);
        expect(AudDspSampler.voiceStats.budget, 24);
        AudDspSampler.voiceBudget = -3;
        expect(AudDspSampler.voiceBudget, 0);
      });

      test('voiceReserve, clamped to 0 to 16', () {
        for (final (voices, kept) in [(3, 3), (-1, 0), (17, 16), (4, 4)]) {
          AudDspSampler.voiceReserve = voices;
          expect(AudDspSampler.voiceReserve, kept, reason: '$voices');
          expect(AudDspSampler.voiceStats.reserve, kept, reason: '$voices');
        }
      });

      test('voiceStats counts the voices of live renders and the steals', () {
        AudDspSampler.voiceBudget = 0;
        AudDspSampler.voiceReserve = 0;
        final graph = samplerGraph();
        final node = graph.createNode(AudSfzSampler.typeId);
        graph.transaction((tx) => tx.connect(node, graph.io));
        graph.start();
        graph.setString(
          node,
          AudSfzSampler.sfzText,
          '<region> sample=*sine ampeg_release=2',
        );
        expect(
          graph.waitLoads(node: node, timeout: const Duration(seconds: 10)),
          isTrue,
        );
        renderLive(graph);
        final before = AudDspSampler.voiceStats;
        for (final note in [60, 62, 64, 66]) {
          play(graph, node, MidiNoteOn(channel: 0, note: note, velocity: 100));
        }
        renderLive(graph);
        // Released after its attack, the note sounds on for two seconds.
        play(graph, node, const MidiNoteOff(channel: 0, note: 60));
        renderLive(graph);
        final playing = AudDspSampler.voiceStats;
        expect(
          (
            playing.sounding - before.sounding,
            playing.released - before.released,
            playing.stolen - before.stolen,
            playing.voiceFrames - before.voiceFrames,
          ),
          (4, 1, 0, 2 * 4 * 256),
        );
        // A budget of two voices: the released voice goes first, then the
        // oldest held one.
        AudDspSampler.voiceBudget = before.sounding + 2;
        renderLive(graph);
        final limited = AudDspSampler.voiceStats;
        expect(
          (
            limited.budget,
            limited.reserve,
            limited.sounding - before.sounding,
            limited.released - before.released,
            limited.stolen - before.stolen,
          ),
          (before.sounding + 2, 0, 2, 0, 2),
        );
        // The node goes: its voices leave the process.
        graph.dispose();
        expect(AudDspSampler.voiceStats.sounding, before.sounding);
      });
    });
  });
}

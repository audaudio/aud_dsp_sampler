// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';
import 'package:test/test.dart';

/// A graph of the engine with the sampler registered and a stereo output;
/// disposed at the end of the test. The notifications wait for [pump].
AudGraphFfi samplerGraph({int maxFrames = 256, double sampleRate = 48000}) {
  final graph = AudGraphFfi(
    listen: false,
    maxFrames: maxFrames,
    sampleRate: sampleRate,
  );
  addTearDown(graph.dispose);
  AudDspSampler.registerWith(graph.hostApi);
  return graph;
}

/// A sampler with [preset] on the output of [graph], which runs; waits for
/// the load.
AudNode samplerNode(AudGraphFfi graph, AudNodePreset preset) {
  final node = graph.createNode(AudSfzSampler.typeId, preset: preset);
  graph.transaction((tx) => tx.connect(node, graph.io));
  graph.start();
  expect(
    graph.waitLoads(node: node, timeout: const Duration(seconds: 10)),
    isTrue,
  );
  return node;
}

/// Sends [message] to [node] at the sample position [at], or with the next
/// block.
void play(AudGraph graph, AudNode node, MidiMessage message, {int? at}) {
  for (final event in AudUmpEvent.fromMessage(message)) {
    graph.sendEvent(
      node,
      event,
      at: at == null ? const AudTimestamp.immediate() : AudTimestamp.sample(at),
    );
  }
}

/// A temporary directory, deleted at the end of the test.
Directory tempDirectory() {
  final directory = Directory.systemTemp.createTempSync('aud_dsp_sampler_');
  addTearDown(() => directory.deleteSync(recursive: true));
  return directory;
}

/// The largest absolute sample of [samples] from [from] to [to].
double peakOf(Float32List samples, [int from = 0, int? to]) {
  var peak = 0.0;
  for (var i = from; i < (to ?? samples.length); i++) {
    peak = math.max(peak, samples[i].abs());
  }
  return peak;
}

/// The frequency of a periodic signal from its rising zero crossings.
double frequencyOf(
  Float32List samples, {
  int from = 0,
  int? to,
  double sampleRate = 48000,
}) {
  int? first;
  var last = 0;
  var count = 0;
  for (var i = from + 1; i < (to ?? samples.length); i++) {
    if (samples[i - 1] < 0 && samples[i] >= 0) {
      first ??= i;
      last = i;
      count += 1;
    }
  }
  if (count < 2) return 0;
  return (count - 1) * sampleRate / (last - first!);
}

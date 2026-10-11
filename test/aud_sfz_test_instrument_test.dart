// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';
import 'package:test/test.dart';

import 'support/sampler_graph.dart';

void main() {
  group('AudSfzTestInstrument', () {
    test('writes the samples and the SFZ file', () {
      final directory = tempDirectory();
      final instrument = AudSfzTestInstrument.write(directory.path);
      expect(instrument.directory, directory.path);
      expect(
        instrument.sfzPath,
        '${directory.path}/${AudSfzTestInstrument.fileName}',
      );
      expect(
        File(instrument.sfzPath).readAsStringSync(),
        AudSfzTestInstrument.sfz,
      );
      for (final key in AudSfzTestInstrument.rootKeys) {
        final file = File(
          '${directory.path}/samples/${AudSfzTestInstrument.sampleName(key)}',
        );
        expect(file.readAsBytesSync(), AudSfzTestInstrument.wav(key));
      }
      // Writing again gives the same files.
      AudSfzTestInstrument.write(directory.path);
      expect(
        File(instrument.sfzPath).readAsStringSync(),
        AudSfzTestInstrument.sfz,
      );
    });

    test('maps every key to one region', () {
      final regions = RegExp(
        r'pitch_keycenter=(\d+) lokey=(\d+) hikey=(\d+)',
      ).allMatches(AudSfzTestInstrument.sfz).toList();
      expect(regions, hasLength(AudSfzTestInstrument.rootKeys.length));
      var next = 0;
      for (final region in regions) {
        final key = int.parse(region.group(1)!);
        final lo = int.parse(region.group(2)!);
        final hi = int.parse(region.group(3)!);
        expect(lo, next);
        expect(key, inInclusiveRange(lo, hi));
        next = hi + 1;
      }
      expect(next, 128);
    });

    test('writes 16-bit saw waves that loop without a seam', () {
      for (final key in AudSfzTestInstrument.rootKeys) {
        final data = ByteData.sublistView(AudSfzTestInstrument.wav(key));
        expect(String.fromCharCodes(data.buffer.asUint8List(0, 4)), 'RIFF');
        expect(data.getUint16(20, Endian.little), 1);
        expect(data.getUint16(22, Endian.little), 1);
        expect(data.getUint32(24, Endian.little), 48000);
        expect(data.getUint16(34, Endian.little), 16);
        expect(data.getUint32(40, Endian.little), 48000 * 2);
        final samples = Float32List(AudSfzTestInstrument.frames);
        for (var i = 0; i < samples.length; i++) {
          samples[i] = data.getInt16(44 + i * 2, Endian.little) / 32768;
        }
        expect(peakOf(samples), lessThan(0.5));
        expect(peakOf(samples), greaterThan(0.3));
        // A whole number of periods in one second.
        expect(
          frequencyOf(samples),
          closeTo(AudSfzTestInstrument.frequencyOf(key), 0.5),
        );
      }
      expect(AudSfzTestInstrument.frequencyOf(69), 440);
      expect(AudSfzTestInstrument.frequencyOf(60), 262);
    });

    test('plays its keys at their pitch', () {
      final directory = tempDirectory();
      final instrument = AudSfzTestInstrument.write(directory.path);
      final graph = samplerGraph();
      final node = samplerNode(graph, instrument.preset(polyphony: 16));
      expect(graph.paramsOf(node)[AudSfzSampler.polyphony], 16);
      final renderer = AudOfflineRenderer(graph);
      renderer.render(frames: 256);
      // The modulation wheel opens the filter, so the fundamental leads.
      play(
        graph,
        node,
        const MidiControlChange(channel: 0, controller: 1, value: 127),
      );
      play(graph, node, const MidiNoteOn(channel: 0, note: 69, velocity: 127));
      final left = renderer.render(frames: 24000)[0][0];
      // Key 69 plays the sample of key 72 three semitones down.
      expect(
        frequencyOf(left, from: 4800),
        closeTo(AudSfzTestInstrument.frequencyOf(72) * pow(2, -3 / 12), 1),
      );
      expect(peakOf(left, 4800), greaterThan(0.05));
    });
  });
}

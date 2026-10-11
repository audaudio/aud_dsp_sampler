// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:typed_data';

import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';
import 'package:test/test.dart';

import 'support/sampler_graph.dart';

// The golden renders of the test instrument (ticket 17, Verification;
// ticket 25): the offline renderer of the graph in blocks of 128 frames at
// 48 kHz, the left channel - the instrument is centred, so the right one is
// the same. Within 1e-5 of the files in test/goldens; UPDATE_GOLDENS=1
// writes them.
void main() {
  group('the golden renders of the test instrument', () {
    late AudSfzTestInstrument instrument;

    setUpAll(() {
      final directory = Directory.systemTemp.createTempSync('aud_goldens_');
      instrument = AudSfzTestInstrument.write(directory.path);
      addTearDown(() => directory.deleteSync(recursive: true));
    });

    // The note on and off of [note] with [velocity] at [at] for [length]
    // frames.
    List<(int, MidiMessage)> note(int at, int length, int note, int velocity) =>
        [
          (at, MidiNoteOn(channel: 0, note: note, velocity: velocity)),
          (at + length, MidiNoteOff(channel: 0, note: note)),
        ];

    MidiMessage cc(int controller, int value) =>
        MidiControlChange(channel: 0, controller: controller, value: value);

    test('envelopes at three velocities', () {
      const step = 14400; // 0.3 s
      final left = _render(
        instrument,
        frames: 3 * step,
        events: [
          ...note(0, 9600, 60, 32),
          ...note(step, 9600, 60, 80),
          ...note(2 * step, 9600, 60, 127),
        ],
      );
      _compare(left, 'envelopes_at_three_velocities');
      // Louder with the velocity; each note fades in its release before the
      // next.
      final peaks = [
        for (var i = 0; i < 3; i++) peakOf(left, i * step, i * step + 9600),
      ];
      expect(peaks[0], lessThan(peaks[1]));
      expect(peaks[1], lessThan(peaks[2]));
      expect(peakOf(left, step - 480, step), lessThan(0.1 * peaks[0]));
    });

    test('velocity response', () {
      const step = 3000;
      const velocities = [16, 32, 48, 64, 80, 96, 112, 127];
      final left = _render(
        instrument,
        frames: 8 * step,
        events: [
          for (var i = 0; i < velocities.length; i++)
            ...note(i * step, 2400, 60, velocities[i]),
        ],
      );
      _compare(left, 'velocity_response');
      var last = 0.0;
      for (var i = 0; i < velocities.length; i++) {
        final peak = peakOf(left, i * step + 240, i * step + 2400);
        expect(peak, greaterThan(last), reason: 'velocity ${velocities[i]}');
        last = peak;
      }
    });

    test('sustain pedal', () {
      final left = _render(
        instrument,
        frames: 24000,
        events: [
          (0, const MidiNoteOn(channel: 0, note: 60, velocity: 100)),
          (4800, cc(64, 127)),
          (9600, const MidiNoteOff(channel: 0, note: 60)),
          (19200, cc(64, 0)),
        ],
      );
      _compare(left, 'sustain_pedal');
      // Held past the note off until the pedal goes up, then released.
      expect(
        peakOf(left, 16800, 19200),
        greaterThan(0.5 * peakOf(left, 4800, 9600)),
      );
      expect(
        peakOf(left, 23500, 24000),
        lessThan(0.2 * peakOf(left, 16800, 19200)),
      );
    });

    test('modulation', () {
      const step = 4800;
      const wheel = [0, 40, 80, 127, 0];
      final left = _render(
        instrument,
        frames: wheel.length * step,
        events: [
          (0, const MidiNoteOn(channel: 0, note: 48, velocity: 32)),
          for (var i = 0; i < wheel.length; i++) (i * step, cc(1, wheel[i])),
        ],
      );
      _compare(left, 'modulation');
      // The wheel opens the filter and closes it again: the overtones grow
      // and shrink. The energy of the steps between frames against the
      // energy of the signal grows with every overtone of a saw wave; it is
      // measured in the second half of each step.
      double brightness(int index) {
        final from = index * step + step ~/ 2;
        var steps = 0.0;
        var energy = 0.0;
        for (var i = from + 1; i < from + step ~/ 2; i++) {
          final difference = left[i] - left[i - 1];
          steps += difference * difference;
          energy += left[i] * left[i];
        }
        return steps / energy;
      }

      expect(brightness(1), greaterThan(brightness(0)));
      expect(brightness(2), greaterThan(brightness(1)));
      expect(brightness(3), greaterThan(2 * brightness(0)));
      expect(brightness(4), lessThan(brightness(3) / 2));
    });

    test('voice stealing', () {
      const step = 2400;
      const notes = [48, 52, 55, 59, 62, 65];
      final left = _render(
        instrument,
        frames: 20000,
        polyphony: 4,
        events: [
          for (var i = 0; i < notes.length; i++)
            (i * step, MidiNoteOn(channel: 0, note: notes[i], velocity: 100)),
        ],
      );
      _compare(left, 'voice_stealing');
      expect(peakOf(left, 18000, 20000), greaterThan(0.05));
    });
  });
}

// Renders the instrument with [events] at their frames and returns the left
// channel; checks that the right one is the same.
Float32List _render(
  AudSfzTestInstrument instrument, {
  required int frames,
  required List<(int, MidiMessage)> events,
  int polyphony = 64,
}) {
  final graph = samplerGraph(maxFrames: 128);
  final node = samplerNode(graph, instrument.preset(polyphony: polyphony));
  final renderer = AudOfflineRenderer(graph);
  renderer.render(frames: 128);
  final start = graph.samplePosition;
  for (final (at, message) in events) {
    play(graph, node, message, at: start + at);
  }
  final output = renderer.render(frames: frames, blockFrames: 128)[0];
  expect(output[1], output[0]);
  return output[0];
}

void _compare(Float32List left, String name) {
  final file = File('test/goldens/$name.wav');
  if (Platform.environment['UPDATE_GOLDENS'] == '1') {
    file.parent.createSync(recursive: true);
    AudWavFile.write(
      file.path,
      AudWavData(channels: [left], sampleRate: 48000),
    );
  }
  expect(file.existsSync(), isTrue, reason: 'run with UPDATE_GOLDENS=1 first');
  final golden = AudWavFile.read(file.path).channels.single;
  expect(golden.length, left.length);
  var largest = 0.0;
  for (var i = 0; i < left.length; i++) {
    final difference = (left[i] - golden[i]).abs();
    if (difference > largest) largest = difference;
  }
  expect(largest, lessThan(1e-5), reason: '$name differs by $largest');
}

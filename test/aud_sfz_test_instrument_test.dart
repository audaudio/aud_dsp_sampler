// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_dsp_sampler/testing.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';
import 'package:test/test.dart';

void main() {
  // A temporary directory, deleted at the end of the test, or of the group
  // when a setUpAll asks for it.
  Directory tempDirectory() {
    final directory = Directory.systemTemp.createTempSync('aud_dsp_sampler_');
    addTearDown(() => directory.deleteSync(recursive: true));
    return directory;
  }

  // A sampler with [preset] on the output of a graph of the engine with
  // blocks of at most [maxFrames]; the graph runs, its notifications wait
  // for a pump, and it is disposed at the end of the test. Waits for the
  // load.
  (AudGraphFfi, AudNode) sampler(AudNodePreset preset, {int maxFrames = 256}) {
    final graph = AudGraphFfi(listen: false, maxFrames: maxFrames);
    addTearDown(graph.dispose);
    AudDspSampler.registerWith(graph.hostApi);
    final node = graph.createNode(AudSfzSampler.typeId, preset: preset);
    graph.transaction((tx) => tx.connect(node, graph.io));
    graph.start();
    expect(
      graph.waitLoads(node: node, timeout: const Duration(seconds: 10)),
      isTrue,
    );
    return (graph, node);
  }

  // Sends [message] to [node] at the sample position [at], or with the
  // next block.
  void play(AudGraph graph, AudNode node, MidiMessage message, {int? at}) {
    for (final event in AudUmpEvent.fromMessage(message)) {
      graph.sendEvent(
        node,
        event,
        at: at == null
            ? const AudTimestamp.immediate()
            : AudTimestamp.sample(at),
      );
    }
  }

  // The largest absolute sample of [samples] from [from] to [to].
  double peakOf(Float32List samples, [int from = 0, int? to]) {
    var peak = 0.0;
    for (var i = from; i < (to ?? samples.length); i++) {
      peak = max(peak, samples[i].abs());
    }
    return peak;
  }

  // The frequency of a periodic signal at 48 kHz from its rising zero
  // crossings after [from].
  double frequencyOf(Float32List samples, {int from = 0}) {
    int? first;
    var last = 0;
    var count = 0;
    for (var i = from + 1; i < samples.length; i++) {
      if (samples[i - 1] < 0 && samples[i] >= 0) {
        first ??= i;
        last = i;
        count += 1;
      }
    }
    if (count < 2) return 0;
    return (count - 1) * 48000 / (last - first!);
  }

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

    for (final key in AudSfzTestInstrument.rootKeys) {
      test('writes the saw wave of key $key, looped without a seam', () {
        final data = ByteData.sublistView(AudSfzTestInstrument.wav(key));
        // RIFF and WAVE, PCM, mono, 48 kHz, 16 bits, one second of data.
        expect(
          [
            String.fromCharCodes(data.buffer.asUint8List(0, 4)),
            String.fromCharCodes(data.buffer.asUint8List(8, 4)),
            data.getUint16(20, Endian.little),
            data.getUint16(22, Endian.little),
            data.getUint32(24, Endian.little),
            data.getUint16(34, Endian.little),
            data.getUint32(40, Endian.little),
          ],
          ['RIFF', 'WAVE', 1, 1, 48000, 16, 96000],
        );
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
      });
    }

    test('rounds the frequency of a key to whole hertz', () {
      const keys = AudSfzTestInstrument.rootKeys;
      expect(AudSfzTestInstrument.frequencyOf(69), 440);
      expect(
        [for (final key in keys) AudSfzTestInstrument.frequencyOf(key)],
        [65, 131, 262, 523, 1047],
      );
      // The cents each root key plays its sample off: C2 the most.
      double centsOff(int key) {
        final exact = 440 * pow(2, (key - 69) / 12);
        return 1200 * log(AudSfzTestInstrument.frequencyOf(key) / exact) / ln2;
      }

      expect(
        [for (final key in keys) centsOff(key).round()],
        [-11, 2, 2, -1, 1],
      );
    });

    test('plays its keys at their pitch', () {
      final directory = tempDirectory();
      final instrument = AudSfzTestInstrument.write(directory.path);
      final (graph, node) = sampler(instrument.preset(polyphony: 16));
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

    // The golden renders of the instrument (ticket 17, Verification; ticket
    // 25): the offline renderer of the graph in blocks of 128 frames at
    // 48 kHz, the left channel - the instrument is centred, so the right one
    // is the same. Within 1e-5 of the files in test/goldens;
    // UPDATE_GOLDENS=1 writes them.
    group('golden renders', () {
      late AudSfzTestInstrument instrument;

      setUpAll(() {
        instrument = AudSfzTestInstrument.write(tempDirectory().path);
      });

      // Renders the instrument with [events] at their frames and returns
      // the left channel; checks that the right one is the same.
      Float32List render({
        required int frames,
        required List<(int, MidiMessage)> events,
        int polyphony = 64,
      }) {
        final (graph, node) = sampler(
          instrument.preset(polyphony: polyphony),
          maxFrames: 128,
        );
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

      // Compares [left] with the golden [name], which UPDATE_GOLDENS=1
      // writes first.
      void compare(Float32List left, String name) {
        final file = File('test/goldens/$name.wav');
        if (Platform.environment['UPDATE_GOLDENS'] == '1') {
          file.parent.createSync(recursive: true);
          AudWavFile.write(
            file.path,
            AudWavData(channels: [left], sampleRate: 48000),
          );
        }
        expect(
          file.existsSync(),
          isTrue,
          reason: 'run with UPDATE_GOLDENS=1 first',
        );
        final golden = AudWavFile.read(file.path).channels.single;
        expect(golden.length, left.length);
        var largest = 0.0;
        for (var i = 0; i < left.length; i++) {
          final difference = (left[i] - golden[i]).abs();
          if (difference > largest) largest = difference;
        }
        expect(largest, lessThan(1e-5), reason: '$name differs by $largest');
      }

      // The note on and off of [note] with [velocity] at [at] for [length]
      // frames.
      List<(int, MidiMessage)> note(
        int at,
        int length,
        int note,
        int velocity,
      ) => [
        (at, MidiNoteOn(channel: 0, note: note, velocity: velocity)),
        (at + length, MidiNoteOff(channel: 0, note: note)),
      ];

      MidiMessage cc(int controller, int value) =>
          MidiControlChange(channel: 0, controller: controller, value: value);

      test('envelopes at three velocities', () {
        const step = 14400; // 0.3 s
        final left = render(
          frames: 3 * step,
          events: [
            ...note(0, 9600, 60, 32),
            ...note(step, 9600, 60, 80),
            ...note(2 * step, 9600, 60, 127),
          ],
        );
        compare(left, 'envelopes_at_three_velocities');
        // Louder with the velocity; each note fades in its release before
        // the next.
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
        final left = render(
          frames: 8 * step,
          events: [
            for (var i = 0; i < velocities.length; i++)
              ...note(i * step, 2400, 60, velocities[i]),
          ],
        );
        compare(left, 'velocity_response');
        // Louder with every velocity: each peak rises above the one before.
        final peaks = [
          for (var i = 0; i < velocities.length; i++)
            peakOf(left, i * step + 240, i * step + 2400),
        ];
        expect([
          for (var i = 0; i < peaks.length; i++)
            peaks[i] - (i == 0 ? 0 : peaks[i - 1]),
        ], everyElement(isPositive));
      });

      test('sustain pedal', () {
        final left = render(
          frames: 24000,
          events: [
            (0, const MidiNoteOn(channel: 0, note: 60, velocity: 100)),
            (4800, cc(64, 127)),
            (9600, const MidiNoteOff(channel: 0, note: 60)),
            (19200, cc(64, 0)),
          ],
        );
        compare(left, 'sustain_pedal');
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
        final left = render(
          frames: wheel.length * step,
          events: [
            (0, const MidiNoteOn(channel: 0, note: 48, velocity: 32)),
            for (var i = 0; i < wheel.length; i++) (i * step, cc(1, wheel[i])),
          ],
        );
        compare(left, 'modulation');
        // The wheel opens the filter and closes it again: the overtones grow
        // and shrink. The energy of the steps between frames against the
        // energy of the signal grows with every overtone of a saw wave; it
        // is measured in the second half of each step.
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
        final left = render(
          frames: 20000,
          polyphony: 4,
          events: [
            for (var i = 0; i < notes.length; i++)
              (i * step, MidiNoteOn(channel: 0, note: notes[i], velocity: 100)),
          ],
        );
        compare(left, 'voice_stealing');
        expect(peakOf(left, 18000, 20000), greaterThan(0.05));
      });
    });
  });
}

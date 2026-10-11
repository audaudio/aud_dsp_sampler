// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core.dart';

import 'aud_sfz_preset.dart';

// #############################################################################
/// An instrument of generated samples for tests, goldens and examples
/// (scope-002: no legacy preset): band-limited saw waves on five root keys,
/// each one second long and looped, with an amplitude envelope, a low-pass
/// filter that follows the velocity and opens with the modulation wheel
/// (CC 1), and sfizz's sustain pedal (CC 64).
///
/// ```dart
/// final instrument = AudSfzTestInstrument.write(directory.path);
/// graph.createNode(AudSfzSampler.typeId, preset: instrument.preset());
/// ```
class AudSfzTestInstrument {
  AudSfzTestInstrument._(this.directory, this.sfzPath);

  /// Writes the samples and the SFZ file into [directory], which it creates;
  /// returns the instrument. Writing again gives the same files.
  static AudSfzTestInstrument write(String directory) {
    final samples = Directory('$directory/samples')
      ..createSync(recursive: true);
    for (final key in rootKeys) {
      File('${samples.path}/${sampleName(key)}').writeAsBytesSync(wav(key));
    }
    final sfzPath = '$directory/$fileName';
    File(sfzPath).writeAsStringSync(sfz);
    return AudSfzTestInstrument._(directory, sfzPath);
  }

  // ...........................................................................
  /// The name of the SFZ file in the directory.
  static const String fileName = 'aud_test_instrument.sfz';

  /// The root keys of the samples: C2 to C6.
  static const List<int> rootKeys = [36, 48, 60, 72, 84];

  /// The sample rate of the samples.
  static const int sampleRate = 48000;

  /// The frames of a sample: one second, a whole number of periods.
  static const int frames = sampleRate;

  /// The frequency of the sample on [key]: the frequency of the key in
  /// equal temperament rounded to whole hertz, so that one second holds a
  /// whole number of periods and the loop has no seam. Its key plays it
  /// at most 4 cents off.
  static int frequencyOf(int key) =>
      (440 * math.pow(2, (key - 69) / 12)).round();

  /// The file name of the sample on [key].
  static String sampleName(int key) => 'saw_$key.wav';

  /// The SFZ text of the instrument; the samples live in `samples/` next to
  /// it.
  static String get sfz {
    final regions = StringBuffer();
    for (var i = 0; i < rootKeys.length; i++) {
      final key = rootKeys[i];
      final lo = i == 0 ? 0 : (rootKeys[i - 1] + key) ~/ 2 + 1;
      final hi = i == rootKeys.length - 1 ? 127 : (key + rootKeys[i + 1]) ~/ 2;
      regions.writeln(
        '<region> sample=${sampleName(key)} pitch_keycenter=$key '
        'lokey=$lo hikey=$hi',
      );
    }
    return '// The test instrument of aud_dsp_sampler, generated (ticket 25).\n'
        '<control>\n'
        'default_path=samples/\n'
        '<global>\n'
        'loop_mode=loop_continuous loop_start=0 loop_end=${frames - 1}\n'
        'ampeg_attack=0.005 ampeg_decay=0.2 ampeg_sustain=60 '
        'ampeg_release=0.25\n'
        'fil_type=lpf_2p cutoff=800 resonance=3 fil_veltrack=2400 '
        'cutoff_oncc1=4800\n'
        '$regions';
  }

  /// The WAV file of the sample on [key]: 16-bit PCM, mono, a band-limited
  /// saw wave of [frequencyOf] with a peak below 0.5 full scale.
  static Uint8List wav(int key) {
    final frequency = frequencyOf(key);
    final harmonics = math.min(40, 20000 ~/ frequency);
    final data = ByteData(44 + frames * 2);
    void text(int at, String value) {
      for (var i = 0; i < value.length; i++) {
        data.setUint8(at + i, value.codeUnitAt(i));
      }
    }

    text(0, 'RIFF');
    data.setUint32(4, 36 + frames * 2, Endian.little);
    text(8, 'WAVE');
    text(12, 'fmt ');
    data.setUint32(16, 16, Endian.little);
    data.setUint16(20, 1, Endian.little); // PCM
    data.setUint16(22, 1, Endian.little); // mono
    data.setUint32(24, sampleRate, Endian.little);
    data.setUint32(28, sampleRate * 2, Endian.little);
    data.setUint16(32, 2, Endian.little);
    data.setUint16(34, 16, Endian.little);
    text(36, 'data');
    data.setUint32(40, frames * 2, Endian.little);
    for (var i = 0; i < frames; i++) {
      var value = 0.0;
      for (var k = 1; k <= harmonics; k++) {
        // The phase in whole frames of the period: exact for every i.
        final phase = (k * frequency * i) % sampleRate;
        final sign = k.isOdd ? 1 : -1;
        value += sign * math.sin(2 * math.pi * phase / sampleRate) / k;
      }
      final sample = (value * 0.26 * 32767).round().clamp(-32768, 32767);
      data.setInt16(44 + i * 2, sample, Endian.little);
    }
    return data.buffer.asUint8List();
  }

  // ...........................................................................
  /// The directory the instrument was written to.
  final String directory;

  /// The path of its SFZ file.
  final String sfzPath;

  /// A preset of the sampler that loads the instrument, with the parameters
  /// of [AudSfzPreset.file].
  AudNodePreset preset({
    String name = 'Test instrument',
    double? volume,
    int? polyphony,
  }) => AudSfzPreset.file(
    sfzPath,
    name: name,
    volume: volume,
    polyphony: polyphony,
  );
}

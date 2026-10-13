// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Proves on an Android device or emulator that the sampler reads an SFZ
// instrument from the assets of the APK in place (ticket 27):
//
//   cd example && flutter test integration_test/apk_assets_test.dart -d <device>
//
// The instrument lies in assets/sfz: its main.sfz includes a file two folders
// up, reaches its sample through default_path with ".." and names the sample
// in another case than its file. The graph renders offline, so the test
// needs no audio device.

import 'dart:math';
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_graph/aud_audio_graph_ffi.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:aud_midi_standard/aud_midi_standard.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

const String instrument = 'apk:/flutter_assets/assets/sfz/presets/x/main.sfz';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  // A graph with a sampler on its output; the load of [file] has ended.
  (AudGraphFfi, AudNode, AudLoad) loadInstrument(String file) {
    final graph = AudGraphFfi(listen: false, maxFrames: 256);
    addTearDown(graph.dispose);
    AudDspSampler.registerWith(graph.hostApi);
    final node = graph.createNode(AudSfzSampler.typeId);
    graph.transaction((tx) => tx.connect(node, graph.io));
    graph.start();
    final id = graph.setString(node, AudSfzSampler.sfzFile, file);
    expect(id, isPositive);
    expect(
      graph.waitLoads(node: node, timeout: const Duration(seconds: 30)),
      isTrue,
    );
    graph.pump();
    return (graph, node, graph.loadOf(id)!);
  }

  // The largest absolute sample of [samples] from [from].
  double peakOf(Float32List samples, int from) {
    var peak = 0.0;
    for (var i = from; i < samples.length; i++) {
      peak = max(peak, samples[i].abs());
    }
    return peak;
  }

  // The frequency of a periodic signal at 48 kHz from its rising zero
  // crossings after [from].
  double frequencyOf(Float32List samples, int from) {
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
    if (first == null || count < 2) return 0;
    return 48000.0 * (count - 1) / (last - first);
  }

  testWidgets('loads an instrument from the assets of the APK and plays it', (
    tester,
  ) async {
    final (graph, node, load) = loadInstrument(instrument);
    expect(load.resultName, 'AUD_OK');
    expect(load.message, isEmpty); // no sample missing
    const note = MidiNoteOn(channel: 0, note: 69, velocity: 100);
    for (final event in AudUmpEvent.fromMessage(note)) {
      graph.sendEvent(node, event);
    }
    final left = AudOfflineRenderer(graph).render(frames: 24000)[0][0];
    expect(peakOf(left, 12000), greaterThan(0.01));
    expect(frequencyOf(left, 12000), closeTo(440, 2));
    expect(AudDspSampler.storeFiles, greaterThanOrEqualTo(1));
  });

  testWidgets('fails a load of an asset the APK does not hold', (tester) async {
    final (_, _, load) = loadInstrument(
      'apk:/flutter_assets/assets/sfz/missing.sfz',
    );
    expect(load.resultName, 'AUD_ERROR_FAILED');
    expect(
      load.message,
      contains(
        'Cannot open file for reading: apk:/flutter_assets/assets/sfz/missing.sfz',
      ),
    );
  });
}

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:convert';

import 'package:aud_dsp_sampler/aud_dsp_sampler_ffi.dart';
import 'package:flutter/material.dart';

// Built with --dart-define=AUD_USE_SAMPLER=true, the app reads the store of
// the sampler and so keeps its native library; without it the release
// build's link hook drops the library, since no member of AudDspSampler is
// reached (interop-001, ticket 25). The example app of aud_audio plays the
// sampler.
const bool useSampler = bool.fromEnvironment('AUD_USE_SAMPLER');

void main() {
  runApp(const AudDspSamplerExampleApp());
}

/// Shows the ids of the sampler node and a preset of it.
class AudDspSamplerExampleApp extends StatelessWidget {
  /// Creates the example app.
  const AudDspSamplerExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    final preset = AudSfzPreset.file('asset:piano', name: 'Piano');
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('aud_dsp_sampler')),
        body: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            const Text('Node type: ${AudSfzSampler.typeId}'),
            Text('Parameters: ${AudSfzSampler.params.join(', ')}'),
            Text('Strings: ${AudSfzSampler.strings.join(', ')}'),
            Text('Preset: ${jsonEncode(preset.toJson())}'),
            if (useSampler)
              Text(
                'Store: ${AudDspSampler.storeFiles} files, '
                '${AudDspSampler.storeBytes} bytes',
              ),
          ],
        ),
      ),
    );
  }
}

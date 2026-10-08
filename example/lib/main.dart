// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';
import 'package:flutter/material.dart';

void main() {
  runApp(const AudDspSamplerExampleApp());
}

/// Names the node types of the package; the example app of `aud_audio`
/// plays them.
class AudDspSamplerExampleApp extends StatelessWidget {
  /// Creates the example app.
  const AudDspSamplerExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(title: const Text('aud_dsp_sampler')),
        body: const Padding(
          padding: EdgeInsets.all(16),
          child: Text('Node types: ${AudSfzSampler.typeId}'),
        ),
      ),
    );
  }
}

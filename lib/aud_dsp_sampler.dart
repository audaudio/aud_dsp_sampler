// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

/// The SFZ sampler node of the Audanika Audio Engine on the package's fork
/// of sfizz, platform-neutral: the ids of the node, its presets and the
/// voice stats of the process. Imports no `dart:ffi`, so it compiles for the
/// web (web-001); `aud_dsp_sampler_ffi.dart` registers the node with a
/// native engine and reads the voices of the process.
library;

export 'src/aud_dsp_sampler_version.dart';
export 'src/aud_sfz_preset.dart';
export 'src/aud_sfz_sampler.dart';
export 'src/aud_sfz_voice_stats.dart';

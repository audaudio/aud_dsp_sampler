// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';

import 'aud_dsp_sampler_bindings_generated.dart' as bindings;

// #############################################################################
/// The SFZ sampler node of the package, rendered by sfizz.
///
/// Load an instrument with the string settings before the node enters the
/// chain: sfizz loads only while no realtime call is in flight.
abstract final class AudSfzSampler {
  /// The type id to create the node with.
  static const String typeId = bindings.AUD_DSP_SAMPLER_SFZ_TYPE_ID;

  /// The index of the volume parameter in dB.
  static const int volume = 0;

  /// The string key that loads an SFZ file from a path.
  static const int sfzFile = bindings.AUD_SFZ_SAMPLER_KEY_FILE;

  /// The string key that sets the virtual path resolving the samples of
  /// [sfzText].
  static const int sfzVirtualPath = bindings.AUD_SFZ_SAMPLER_KEY_VIRTUAL_PATH;

  /// The string key that loads SFZ text relative to [sfzVirtualPath].
  static const int sfzText = bindings.AUD_SFZ_SAMPLER_KEY_TEXT;
}

// #############################################################################
/// Thrown when the package cannot register its node types.
class AudDspSamplerException implements Exception {
  /// Creates the exception for a result [code].
  const AudDspSamplerException({required this.code});

  /// The result code, one of the `AUD_ERROR_*` constants.
  final int code;

  @override
  String toString() =>
      'AudDspSamplerException: registration failed with '
      '${AudAbi.resultName(code)}';
}

// #############################################################################
/// The registration of the package's node types with an engine.
abstract final class AudDspSampler {
  /// Registers the node types with the host api of an engine, e.g.
  /// `AudEngine.hostApi`; throws when the engine refuses them.
  static void register(Pointer<Void> hostApi) {
    final result = bindings.aud_dsp_sampler_register(hostApi);
    if (result != AUD_OK) throw AudDspSamplerException(code: result);
  }
}

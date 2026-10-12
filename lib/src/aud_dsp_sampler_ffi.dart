// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:meta/meta.dart';

import 'aud_dsp_sampler_bindings_generated.dart' as bindings;

// #############################################################################
/// The native side of the package: the registration of the sampler with an
/// engine and the numbers of its sample store.
///
/// Every member is recorded (`@RecordUse`): in a profile or release build
/// the link hook of the package keeps its native library - sfizz with the
/// node - only when an app uses one of them.
///
/// ```dart
/// final engine = AudEngine(packages: [
///   AudNativeNodePackage(AudDspSampler.name, AudDspSampler.register),
/// ]);
/// ```
abstract final class AudDspSampler {
  /// The name of the package, e.g. for an engine's package list.
  static const String name = 'aud_dsp_sampler';

  /// The register function of the package,
  /// `aud_dsp_sampler_register(const AudHostApi*)`, which an engine calls
  /// with its host api.
  @RecordUse()
  static core.AudRegisterFunction get register =>
      Native.addressOf<NativeFunction<core.AudRegisterFunctionFunction>>(
        bindings.aud_dsp_sampler_register,
      );

  /// Registers the node types with the engine behind [host], e.g.
  /// `AudGraphFfi.hostApi`; throws an [AudDspSamplerException] when the
  /// engine refuses them.
  @RecordUse()
  static void registerWith(Pointer<core.AudHostApi> host) {
    final result = bindings.aud_dsp_sampler_register(host);
    if (result != AUD_OK) throw AudDspSamplerException(code: result);
  }

  /// The bytes the sample store of the process holds: the heads of the
  /// loaded instruments and the streamed files the budget has not trimmed.
  @RecordUse()
  static int get storeBytes => bindings.aud_dsp_sampler_store_bytes();

  /// The sample files the store of the process keeps.
  @RecordUse()
  static int get storeFiles => bindings.aud_dsp_sampler_store_files();
}

// #############################################################################
/// Thrown when an engine refuses the node types of the package.
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

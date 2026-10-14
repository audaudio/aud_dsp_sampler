// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_audio_core/aud_audio_core_bindings.dart' as core;
import 'package:ffi/ffi.dart';
import 'package:meta/meta.dart';

import 'aud_dsp_sampler_bindings_generated.dart' as bindings;
import 'aud_sfz_voice_stats.dart';

// #############################################################################
/// The native side of the package: the registration of the sampler with an
/// engine, the numbers of its sample store and the voice budget of the
/// process.
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

  // ...........................................................................
  // The voices of the process (ticket 29)

  /// The voice budget of the process: the voices that all sampler nodes of
  /// all graphs keep sounding together in live renders, minus
  /// [voiceReserve]; 0, the default, sets none.
  @RecordUse()
  static int get voiceBudget => bindings.aud_dsp_sampler_voice_budget();

  /// Sets the voice budget of the process; a negative budget counts as 0,
  /// which sets none. The nodes follow at the end of their next block and
  /// steal the voices above it with a fade of 10 ms: released voices first,
  /// then the oldest.
  @RecordUse()
  static set voiceBudget(int voices) =>
      bindings.aud_dsp_sampler_set_voice_budget(voices);

  /// The free voices kept below the voice budget and below the
  /// `max_voices` of every node, so that a new note finds a free voice
  /// while a stolen one fades; 4 by default.
  @RecordUse()
  static int get voiceReserve => bindings.aud_dsp_sampler_voice_reserve();

  /// Sets the free voices kept below the voice budget and every
  /// `max_voices`, clamped to 0 to 16.
  @RecordUse()
  static set voiceReserve(int voices) =>
      bindings.aud_dsp_sampler_set_voice_reserve(voices);

  /// The voices of the process: the budget, the reserve, the voices the
  /// live nodes sound and the counters of the voices stolen and rendered.
  @RecordUse()
  static AudSfzVoiceStats get voiceStats {
    final stats = calloc<bindings.AudSfzVoiceStats>();
    try {
      stats.ref.struct_size = sizeOf<bindings.AudSfzVoiceStats>();
      // Takes any struct of its size; nothing else fails.
      bindings.aud_dsp_sampler_voice_stats(stats);
      final ref = stats.ref;
      return AudSfzVoiceStats(
        budget: ref.budget,
        reserve: ref.reserve,
        sounding: ref.sounding,
        released: ref.released,
        stolen: ref.stolen,
        voiceFrames: ref.voice_frames,
      );
    } finally {
      calloc.free(stats);
    }
  }
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

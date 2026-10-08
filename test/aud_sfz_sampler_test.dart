// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:ffi';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:aud_audio_core/aud_audio_core.dart';
import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';
import 'package:ffi/ffi.dart';
import 'package:test/test.dart';

// A fake host: records the descriptors a package registers. Registration
// runs synchronously on the calling thread, so an isolate-local callable
// serves as the callback.
class FakeHost {
  FakeHost({this.result = AUD_OK, int abiMajor = AudAbi.major}) {
    api = calloc<AudHostApi>();
    api.ref
      ..struct_size = sizeOf<AudHostApi>()
      ..abi_major = abiMajor
      ..abi_minor = AudAbi.minor
      ..host = api.cast()
      ..register_node_type = _register.nativeFunction;
  }

  final int result;
  late final Pointer<AudHostApi> api;
  final List<Pointer<AudNodeDescriptor>> descriptors = [];
  late final _register =
      NativeCallable<
        Int32 Function(Pointer<Void>, Pointer<AudNodeDescriptor>)
      >.isolateLocal(_onRegister, exceptionalReturn: AUD_ERROR_FAILED);

  int _onRegister(Pointer<Void> host, Pointer<AudNodeDescriptor> descriptor) {
    expect(host, api.cast<Void>());
    descriptors.add(descriptor);
    return result;
  }

  void dispose() {
    _register.close();
    calloc.free(api);
  }
}

// Drives a node instance through the function pointers of its vtable.
class NodeDriver {
  NodeDriver(this.descriptor, this.host) {
    final vtable = descriptor.ref.vtable.ref;
    instance = vtable.create
        .asFunction<
          Pointer<Void> Function(
            Pointer<AudNodeDescriptor>,
            Pointer<AudHostApi>,
          )
        >()(descriptor, host);
    _prepare = vtable.prepare
        .asFunction<int Function(Pointer<Void>, double, int, int)>();
    _reset = vtable.reset.asFunction<void Function(Pointer<Void>)>();
    _setParam = vtable.set_param
        .asFunction<void Function(Pointer<Void>, int, double)>();
    _event = vtable.event
        .asFunction<void Function(Pointer<Void>, Pointer<AudEvent>)>();
    _process = vtable.process
        .asFunction<void Function(Pointer<Void>, Pointer<AudProcessContext>)>();
    _setString = vtable.set_string
        .asFunction<int Function(Pointer<Void>, int, Pointer<Char>)>();
    _destroy = vtable.destroy.asFunction<void Function(Pointer<Void>)>();
  }

  final Pointer<AudNodeDescriptor> descriptor;
  final Pointer<AudHostApi> host;
  late final Pointer<Void> instance;
  late final int Function(Pointer<Void>, double, int, int) _prepare;
  late final void Function(Pointer<Void>) _reset;
  late final void Function(Pointer<Void>, int, double) _setParam;
  late final void Function(Pointer<Void>, Pointer<AudEvent>) _event;
  late final void Function(Pointer<Void>, Pointer<AudProcessContext>) _process;
  late final int Function(Pointer<Void>, int, Pointer<Char>) _setString;
  late final void Function(Pointer<Void>) _destroy;

  int prepare(double sampleRate, int maxFrames, int channels) =>
      _prepare(instance, sampleRate, maxFrames, channels);

  void reset() => _reset(instance);

  void setParam(int index, double value) => _setParam(instance, index, value);

  int setString(int key, String value) {
    final native = value.toNativeUtf8();
    try {
      return _setString(instance, key, native.cast());
    } finally {
      calloc.free(native);
    }
  }

  void note(int type, int number, double velocity) {
    final event = calloc<AudEvent>();
    event.ref
      ..struct_size = sizeOf<AudEvent>()
      ..type = type
      ..number = number
      ..value = velocity;
    _event(instance, event);
    calloc.free(event);
  }

  // Renders one block of [frames] frames and returns the planar channels.
  List<Float32List> process(int frames, int channels) {
    final buffers = calloc<Pointer<Float>>(channels);
    for (var c = 0; c < channels; c++) {
      buffers[c] = calloc<Float>(frames);
    }
    final context = calloc<AudProcessContext>();
    context.ref
      ..struct_size = sizeOf<AudProcessContext>()
      ..frames = frames
      ..channels = channels
      ..sample_rate = 48000
      ..inputs = nullptr
      ..outputs = buffers;
    _process(instance, context);
    final result = [
      for (var c = 0; c < channels; c++)
        Float32List.fromList(buffers[c].asTypedList(frames)),
    ];
    for (var c = 0; c < channels; c++) {
      calloc.free(buffers[c]);
    }
    calloc.free(buffers);
    calloc.free(context);
    return result;
  }

  void destroy() => _destroy(instance);
}

// Writes a 16 bit mono WAV with a 440 Hz sine of [seconds] seconds.
File writeSineWav(Directory directory, double seconds) {
  const rate = 44100;
  final frames = (rate * seconds).round();
  final bytes = ByteData(44 + frames * 2);
  void ascii(int offset, String text) {
    for (var i = 0; i < text.length; i++) {
      bytes.setUint8(offset + i, text.codeUnitAt(i));
    }
  }

  ascii(0, 'RIFF');
  bytes.setUint32(4, 36 + frames * 2, Endian.little);
  ascii(8, 'WAVE');
  ascii(12, 'fmt ');
  bytes.setUint32(16, 16, Endian.little);
  bytes.setUint16(20, 1, Endian.little);
  bytes.setUint16(22, 1, Endian.little);
  bytes.setUint32(24, rate, Endian.little);
  bytes.setUint32(28, rate * 2, Endian.little);
  bytes.setUint16(32, 2, Endian.little);
  bytes.setUint16(34, 16, Endian.little);
  ascii(36, 'data');
  bytes.setUint32(40, frames * 2, Endian.little);
  for (var i = 0; i < frames; i++) {
    final sample = sin(2 * pi * 440 * i / rate) * 0.5;
    bytes.setInt16(44 + i * 2, (sample * 32767).round(), Endian.little);
  }
  final file = File('${directory.path}/sine.wav');
  file.writeAsBytesSync(bytes.buffer.asUint8List());
  return file;
}

double peak(List<Float32List> channels) =>
    channels.expand((c) => c).fold(0.0, (m, s) => max(m, s.abs()));

void main() {
  group('AudSfzSampler', () {
    test('names the type, the parameter and the string keys', () {
      expect(AudSfzSampler.typeId, 'aud.sampler.sfz');
      expect(AudSfzSampler.volume, 0);
      expect(AudSfzSampler.sfzFile, 0);
      expect(AudSfzSampler.sfzVirtualPath, 1);
      expect(AudSfzSampler.sfzText, 2);
    });
  });

  group('AudDspSampler.register(hostApi)', () {
    late FakeHost host;

    tearDown(() => host.dispose());

    test('registers the sampler with the ABI version it was built against', () {
      host = FakeHost();
      AudDspSampler.register(host.api.cast());
      final descriptor = host.descriptors.single.ref;
      expect(descriptor.type_id.cast<Utf8>().toDartString(), 'aud.sampler.sfz');
      expect(descriptor.abi_major, AudAbi.major);
      expect(descriptor.abi_minor, AudAbi.minor);
      expect(descriptor.capabilities & AUD_NODE_CAP_EVENTS, isNonZero);
      expect(descriptor.capabilities & AUD_NODE_CAP_STRINGS, isNonZero);
      expect(descriptor.num_inputs, 0);
      expect(descriptor.num_outputs, 1);
      expect(descriptor.num_params, 1);
      expect(descriptor.params[0].unit.cast<Utf8>().toDartString(), 'dB');
      expect(descriptor.vtable.ref.event, isNot(nullptr));
      expect(descriptor.vtable.ref.set_string, isNot(nullptr));
    });

    test('throws when the host refuses the node type', () {
      host = FakeHost(result: AUD_ERROR_DUPLICATE_TYPE);
      expect(
        () => AudDspSampler.register(host.api.cast()),
        throwsA(
          isA<AudDspSamplerException>()
              .having((e) => e.code, 'code', AUD_ERROR_DUPLICATE_TYPE)
              .having((e) => e.toString(), 'toString', contains('DUPLICATE')),
        ),
      );
    });

    test('refuses a host of another ABI major and a null host', () {
      host = FakeHost(abiMajor: AudAbi.major + 1);
      expect(
        () => AudDspSampler.register(host.api.cast()),
        throwsA(
          isA<AudDspSamplerException>().having(
            (e) => e.code,
            'code',
            AUD_ERROR_ABI_MAJOR,
          ),
        ),
      );
      expect(
        () => AudDspSampler.register(nullptr),
        throwsA(
          isA<AudDspSamplerException>().having(
            (e) => e.code,
            'code',
            AUD_ERROR_INVALID_ARGUMENT,
          ),
        ),
      );
      expect(host.descriptors, isEmpty);
    });
  });

  group('the sampler node through its vtable', () {
    late FakeHost host;
    late NodeDriver node;
    late Directory directory;

    setUp(() {
      host = FakeHost();
      AudDspSampler.register(host.api.cast());
      node = NodeDriver(host.descriptors.single, host.api);
      directory = Directory.systemTemp.createTempSync('aud_dsp_sampler_');
      writeSineWav(directory, 0.5);
    });

    tearDown(() {
      node.destroy();
      host.dispose();
      directory.deleteSync(recursive: true);
    });

    test('plays a note of an SFZ loaded from text', () {
      expect(node.prepare(48000, 256, 2), AUD_OK);
      expect(
        node.setString(AudSfzSampler.sfzVirtualPath, '${directory.path}/v.sfz'),
        AUD_OK,
      );
      expect(
        node.setString(
          AudSfzSampler.sfzText,
          '<region> sample=sine.wav pitch_keycenter=69 lokey=0 hikey=127',
        ),
        AUD_OK,
      );
      expect(peak(node.process(256, 2)), 0, reason: 'silent before a note');
      node.note(AUD_EVENT_NOTE_ON, 69, 0.8);
      var loudest = 0.0;
      for (var block = 0; block < 8; block++) {
        loudest = max(loudest, peak(node.process(256, 2)));
      }
      expect(loudest, greaterThan(0.02));
      node.setParam(AudSfzSampler.volume, -60);
      node.note(AUD_EVENT_CONTROL, 7, 1);
      node.note(AUD_EVENT_NOTE_OFF, 69, 0);
      node.note(99, 69, 0);
      node.reset();
      var silent = 0.0;
      for (var block = 0; block < 8; block++) {
        silent = max(silent, peak(node.process(256, 2)));
      }
      expect(silent, lessThan(loudest));
    });

    test('loads an SFZ file and reports failures', () {
      expect(node.prepare(44100, 128, 2), AUD_OK);
      final sfz = File('${directory.path}/file.sfz')
        ..writeAsStringSync('<region> sample=sine.wav key=60');
      expect(node.setString(AudSfzSampler.sfzFile, sfz.path), AUD_OK);
      expect(
        node.setString(AudSfzSampler.sfzFile, '${directory.path}/missing.sfz'),
        AUD_ERROR_FAILED,
      );
      expect(node.setString(7, 'value'), AUD_ERROR_INVALID_ARGUMENT);
      expect(
        node.setString(AudSfzSampler.sfzText, '<region> sample=none.wav'),
        AUD_OK,
        reason: 'missing samples are tolerated by sfizz',
      );
    });

    test('refuses an odd channel count', () {
      expect(node.prepare(48000, 256, 1), AUD_ERROR_INVALID_ARGUMENT);
      expect(node.prepare(48000, 256, 0), AUD_ERROR_INVALID_ARGUMENT);
    });
  });
}

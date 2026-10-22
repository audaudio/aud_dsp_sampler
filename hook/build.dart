// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:isolate';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:logging/logging.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

// Builds the sampler node together with the package's fork of sfizz: the
// sources in src/third_party/sfizz/SOURCES.txt and the include directories
// in INCLUDES.txt, both relative to that directory. The sources mix C and
// C++, so no language flag is passed - clang picks the language by file
// extension - and the C++ runtime is linked explicitly. The ABI header
// comes from aud_audio_core, resolved through the package config.
void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;
    final packageName = input.packageName;
    final targetOS = input.config.code.targetOS;
    final android = targetOS == OS.android;
    final apple = targetOS == OS.iOS || targetOS == OS.macOS;
    final windows = targetOS == OS.windows;
    const sfizz = 'src/third_party/sfizz';
    final cbuilder = CBuilder.library(
      name: packageName,
      assetName: 'src/${packageName}_bindings_generated.dart',
      sources: [
        'src/$packageName.cpp',
        'src/aud_apk_assets.cpp',
        'src/aud_sfz_node.cpp',
        'src/aud_sfz_voice_budget.cpp',
        ...await uniqueObjectNames(
          await listOf(input.packageRoot, '$sfizz/SOURCES.txt', sfizz),
          input.packageRoot,
          input.outputDirectory,
          windows: windows,
        ),
      ],
      includes: [
        'src',
        await packageSrcDirectory('aud_audio_core'),
        ...await listOf(input.packageRoot, '$sfizz/INCLUDES.txt', sfizz),
      ],
      language: Language.c,
      flags: [
        if (!windows) '-fvisibility=hidden',
        // sfizz's AVX objects carry no __AVX__ guard; on x86_64 the whole
        // library is built for AVX because a hook has no per-file flags.
        if (input.config.code.targetArchitecture == Architecture.x64)
          windows ? '/arch:AVX' : '-mavx',
        if (windows) ...[
          // simde needs <cmath>, see src/aud_windows_math.h.
          '/FIaud_windows_math.h',
          // sfizz throws, and ghc::filesystem offers its throwing
          // overloads only with exceptions.
          '/EHsc',
          // The C units of st_audiofile use C11 atomics; the C++ units
          // stay at MSVC's C++14, as sfizz and the node build elsewhere.
          // MSVC takes one of /std:c11 and /std:c++17, not both.
          '/std:c11',
          '/experimental:c11atomics',
          '/utf-8',
        ],
        // A symbol the library leaves undefined fails the link, not the
        // load in the app: on armeabi-v7a, Abseil's 128-bit division was
        // such a symbol (ticket 27).
        if (android) '-Wl,--no-undefined',
      ],
      defines: {
        'GHC_FILESYSTEM_FWD': null,
        'ENABLE_DSD': null,
        'ENABLE_THREADS': null,
        'HAVE_FSEEKO': null,
        // MSVC has no __builtin_clz; wavpack counts the bits itself there.
        if (!windows) 'HAVE___BUILTIN_CLZ': null,
        'NOMINMAX': null,
        // M_PI and its kin in the C runtime of Windows.
        '_USE_MATH_DEFINES': null,
      },
      libraries: [
        // `android` holds AAssetManager, through which apk:/ paths read the
        // assets of the app (ticket 27).
        if (android) ...['c++_static', 'c++abi', 'log', 'm', 'android'],
        if (apple) 'c++',
        if (targetOS == OS.linux) 'stdc++',
        // CommandLineToArgvW of ghc::filesystem.
        if (windows) 'shell32',
      ],
    );
    await cbuilder.run(
      input: input,
      output: output,
      // In profile and release builds the link hook decides whether the
      // app uses the sampler (interop-001, ticket 25).
      routing: [
        if (input.config.linkingEnabled)
          ToLinkHook(packageName)
        else
          const ToAppBundle(),
      ],
      logger: Logger('')
        ..level = Level.ALL
        ..onRecord.listen((record) => stdout.writeln(record.message)),
    );
  });
}

/// [sources] with a unique file name each when the target is Windows: MSVC
/// writes the objects of all units into one directory, so sfizz's
/// modulations/sources/LFO.cpp would overwrite the object of LFO.cpp. Every
/// further file of a name gets a wrapper in [outputDirectory] that includes
/// it (ticket 38).
Future<List<String>> uniqueObjectNames(
  List<String> sources,
  Uri packageRoot,
  Uri outputDirectory, {
  required bool windows,
}) async {
  if (!windows) return sources;
  final seen = <String>{};
  final result = <String>[];
  for (final source in sources) {
    final name = source.split('/').last.toLowerCase();
    if (seen.add(name)) {
      result.add(source);
      continue;
    }
    final wrapper = File.fromUri(
      outputDirectory.resolve('unique/${source.replaceAll('/', '_')}'),
    );
    await wrapper.parent.create(recursive: true);
    final original = packageRoot
        .resolve(source)
        .toFilePath()
        .replaceAll(r'\', '/');
    await wrapper.writeAsString('#include "$original"\n');
    result.add(wrapper.path);
  }
  return result;
}

/// The `src` directory of [package], resolved through the package config.
Future<String> packageSrcDirectory(String package) async {
  final lib = await Isolate.resolvePackageUri(Uri.parse('package:$package/'));
  if (lib == null) throw StateError('Package $package is not resolvable');
  return lib.resolve('../src/').toFilePath();
}

/// The non-empty lines of [file] under [packageRoot], prefixed with
/// [directory].
Future<List<String>> listOf(
  Uri packageRoot,
  String file,
  String directory,
) async {
  final lines = await File.fromUri(packageRoot.resolve(file)).readAsLines();
  return [
    for (final line in lines)
      if (line.trim().isNotEmpty) '$directory/${line.trim()}',
  ];
}

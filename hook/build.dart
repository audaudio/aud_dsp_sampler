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

// Builds the sampler node together with the vendored subset of sfizz: the
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
    const sfizz = 'src/third_party/sfizz';
    final cbuilder = CBuilder.library(
      name: packageName,
      assetName: 'src/${packageName}_bindings_generated.dart',
      sources: [
        'src/$packageName.cpp',
        ...await listOf(input.packageRoot, '$sfizz/SOURCES.txt', sfizz),
      ],
      includes: [
        'src',
        await packageSrcDirectory('aud_audio_core'),
        ...await listOf(input.packageRoot, '$sfizz/INCLUDES.txt', sfizz),
      ],
      language: Language.c,
      flags: [
        '-fvisibility=hidden',
        // sfizz's AVX objects carry no __AVX__ guard; on x86_64 the whole
        // library is built for AVX because a hook has no per-file flags.
        if (input.config.code.targetArchitecture == Architecture.x64) '-mavx',
      ],
      defines: const {
        'GHC_FILESYSTEM_FWD': null,
        'ENABLE_DSD': null,
        'ENABLE_THREADS': null,
        'HAVE_FSEEKO': null,
        'HAVE___BUILTIN_CLZ': null,
        'NOMINMAX': null,
      },
      libraries: [
        if (android) ...['c++_static', 'c++abi', 'log', 'm'],
        if (apple) 'c++',
        if (!android && !apple) 'stdc++',
      ],
    );
    await cbuilder.run(
      input: input,
      output: output,
      logger: Logger('')
        ..level = Level.ALL
        ..onRecord.listen((record) => stdout.writeln(record.message)),
    );
  });
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

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';

import 'package:test/test.dart';

import '../../hook/build.dart';

void main() {
  late Directory root;
  late Directory out;

  setUp(() {
    root = Directory.systemTemp.createTempSync('aud_dsp_sampler_hook_');
    out = Directory('${root.path}${Platform.pathSeparator}out')..createSync();
    for (final source in _sources) {
      File.fromUri(root.uri.resolve(source)).createSync(recursive: true);
    }
  });

  tearDown(() => root.deleteSync(recursive: true));

  File wrapperOf(String source) =>
      File.fromUri(out.uri.resolve('unique/${source.replaceAll('/', '_')}'));

  Future<List<String>> unique({bool windows = true}) =>
      uniqueObjectNames(_sources, root.uri, out.uri, windows: windows);

  group('hook/build.dart', () {
    group('uniqueObjectNames', () {
      test('keeps the sources off Windows', () async {
        expect(await unique(windows: false), _sources);
        expect(Directory('${out.path}/unique').existsSync(), isFalse);
      });

      test('wraps every further file of a name on Windows', () async {
        expect(await unique(), [
          'a/LFO.cpp',
          wrapperOf('b/lfo.cpp').path,
          'c/Other.cpp',
        ]);
        final original = File.fromUri(root.uri.resolve('b/lfo.cpp')).path;
        expect(
          wrapperOf('b/lfo.cpp').readAsStringSync(),
          '#include "${original.replaceAll(r'\', '/')}"\n',
        );
      });

      test('dates a new wrapper back to its source', () async {
        final sourceTime = DateTime(2021, 3, 4);
        File.fromUri(root.uri.resolve('b/lfo.cpp'))
            .setLastModifiedSync(sourceTime);
        await unique();
        expect(
          wrapperOf('b/lfo.cpp')
              .lastModifiedSync()
              .isAtSameMomentAs(sourceTime),
          isTrue,
        );
      });

      test('leaves an unchanged wrapper untouched', () async {
        await unique();
        final wrapper = wrapperOf('b/lfo.cpp');
        final earlier = DateTime(2020, 1, 2);
        wrapper.setLastModifiedSync(earlier);
        await unique();
        expect(wrapper.lastModifiedSync().isAtSameMomentAs(earlier), isTrue);
      });
    });

    group('writeIfChanged', () {
      test('rewrites a file whose content differs', () async {
        final source = File('${root.path}/source.cpp')
          ..createSync()
          ..setLastModifiedSync(DateTime(2022, 5, 6));
        final file = File('${root.path}/wrapper.cpp')..writeAsStringSync('old');
        await writeIfChanged(file, 'new', source);
        expect(file.readAsStringSync(), 'new');
        expect(
          file.lastModifiedSync().isAtSameMomentAs(DateTime(2022, 5, 6)),
          isTrue,
        );
      });
    });
  });
}

const _sources = ['a/LFO.cpp', 'b/lfo.cpp', 'c/Other.cpp'];

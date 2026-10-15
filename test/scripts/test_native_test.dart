// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';

import 'package:test/test.dart';

void main() {
  group('scripts/test-native.js', () {
    test('builds and runs the native tests with the sanitizers', () {
      final result = Process.runSync(
        'node',
        [
          'scripts/test-native.js',
          if (Platform.environment['AUD_NO_SANITIZE'] == '1') '--no-sanitize',
        ],
        // Every test runs, whatever filter the shell set.
        environment: {'AUD_TEST_FILTER': ''},
      );
      final output = '${result.stdout}\n${result.stderr}';
      expect(result.exitCode, 0, reason: output);
      // The summary of each build: "113 of 113 native tests passed, 0 checks
      // failed". Every build ran tests, and all of them passed.
      final builds = [
        for (final match in RegExp(
          r'(\d+) of (\d+) native tests passed, (\d+) checks failed',
        ).allMatches('${result.stdout}'))
          (int.parse(match[1]!), int.parse(match[2]!), int.parse(match[3]!)),
      ];
      expect(builds, isNotEmpty, reason: output);
      expect(builds, [for (final (_, ran, _) in builds) (ran, ran, 0)]);
      expect([for (final (_, ran, _) in builds) ran], everyElement(isPositive));
    }, timeout: const Timeout(Duration(minutes: 15)));

    group('options', () {
      // Runs the script with options it refuses or answers before it
      // builds anything.
      ProcessResult run(List<String> args) =>
          Process.runSync('node', ['scripts/test-native.js', ...args]);

      test('--help prints the usage with --android', () {
        final result = run(['--help']);
        expect(result.exitCode, 0, reason: '${result.stderr}');
        expect(
          '${result.stdout}',
          allOf(
            startsWith('Usage: node scripts/test-native.js [options]'),
            contains('--android[=<adb serial>]'),
            contains('ANDROID_NDK_HOME=<ndk>'),
            contains('default 0.3.2'),
          ),
        );
      });

      for (final (args, message) in [
        (['--android'], '--android needs --bench'),
        (['--android=ce06193672a0ae0e057e'], '--android needs --bench'),
        (['--bench', '--android='], 'Unknown option --android='),
        (['--bench', '--androids'], 'Unknown option --androids'),
      ]) {
        test('${args.join(' ')} ends with the usage and exit code 2', () {
          final result = run(args);
          expect(result.exitCode, 2);
          expect('${result.stdout}', isEmpty);
          expect(
            '${result.stderr}',
            allOf(startsWith('$message\n\n'), contains('Usage:')),
          );
        });
      }
    });
  });
}

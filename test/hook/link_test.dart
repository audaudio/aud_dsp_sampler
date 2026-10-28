// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'dart:io';
import 'dart:isolate';

import 'package:record_use/record_use.dart';
import 'package:test/test.dart';

import '../../hook/link.dart';

void main() {
  group('hook/link.dart', () {
    test('keeps the library without recordings', () {
      expect(usesSampler(null), isTrue);
    });

    test('drops the library of an app that calls no member', () {
      expect(usesSampler(Recordings(calls: {}, instances: {})), isFalse);
      final other = Method(
        'speak',
        Class('Other', Library('package:other/other.dart')),
      );
      expect(
        usesSampler(
          Recordings(
            calls: {
              other: [_call],
            },
            instances: {},
          ),
        ),
        isFalse,
      );
      // Members the recordings name without a call.
      expect(
        usesSampler(
          Recordings(
            calls: {for (final member in samplerMembers) member: []},
            instances: {},
          ),
        ),
        isFalse,
      );
    });

    for (final member in samplerMembers) {
      test('keeps the library of an app that calls ${member.name}', () {
        expect(
          usesSampler(
            Recordings(
              calls: {
                member: [_call],
              },
              instances: {},
            ),
          ),
          isTrue,
        );
      });
    }

    test('names every member that AudDspSampler records', () async {
      final file = await Isolate.resolvePackageUri(Uri.parse(samplerLibrary));
      final source = File.fromUri(file!).readAsStringSync();
      final body = RegExp(r'class AudDspSampler \{([\s\S]*?)\n\}')
          .firstMatch(source)!
          .group(1)!;
      const sampler = Class('AudDspSampler', Library(samplerLibrary));
      // The declaration after each @RecordUse() up to its parameters or its
      // body, e.g. `static int get storeBytes`.
      final recorded = [
        for (final match in RegExp(
          r'@RecordUse\(\)\s+([^(={;]+)',
        ).allMatches(body))
          switch (match.group(1)!.trim().split(RegExp(r'\s+'))) {
            final words when words.contains('get') => Getter(
              words.last,
              sampler,
              isInstanceMember: !words.contains('static'),
            ),
            final words when words.contains('set') => Setter(
              words.last,
              sampler,
              isInstanceMember: !words.contains('static'),
            ),
            final words => Method(
              words.last,
              sampler,
              isInstanceMember: !words.contains('static'),
            ),
          },
      ];
      expect(recorded, unorderedEquals(samplerMembers));
    });
  });
}

const _call = CallWithArguments(
  positionalArguments: [],
  namedArguments: {},
  loadingUnit: LoadingUnit('main'),
);

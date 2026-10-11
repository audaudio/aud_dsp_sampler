// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:record_use/record_use.dart';
import 'package:test/test.dart';

import '../../hook/link.dart';

void main() {
  group('hook/link.dart', () {
    test('keeps the library without recordings', () {
      expect(usesSampler(null), isTrue);
    });

    test('drops the library of an app that uses no member', () {
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
    });

    test('keeps the library of an app that uses any member', () {
      for (final member in samplerMembers) {
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
          reason: '$member',
        );
        expect(
          usesSampler(Recordings(calls: {member: []}, instances: {})),
          isFalse,
        );
      }
      expect(samplerMembers, hasLength(4));
    });
  });
}

const _call = CallWithArguments(
  positionalArguments: [],
  namedArguments: {},
  loadingUnit: LoadingUnit('main'),
);

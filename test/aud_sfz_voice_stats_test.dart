// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:aud_dsp_sampler/aud_dsp_sampler.dart';
import 'package:test/test.dart';

void main() {
  group('AudSfzVoiceStats', () {
    const stats = AudSfzVoiceStats(
      budget: 48,
      reserve: 4,
      sounding: 40,
      released: 12,
      stolen: 7,
      voiceFrames: 123456,
    );

    test('starts at zero', () {
      const zero = AudSfzVoiceStats();
      expect(
        (
          zero.budget,
          zero.reserve,
          zero.sounding,
          zero.released,
          zero.stolen,
          zero.voiceFrames,
        ),
        (0, 0, 0, 0, 0, 0),
      );
    });

    test('toJson()', () {
      expect(stats.toJson(), {
        'budget': 48,
        'reserve': 4,
        'sounding': 40,
        'released': 12,
        'stolen': 7,
        'voiceFrames': 123456,
      });
    });

    test('==, hashCode and toString()', () {
      const same = AudSfzVoiceStats(
        budget: 48,
        reserve: 4,
        sounding: 40,
        released: 12,
        stolen: 7,
        voiceFrames: 123456,
      );
      expect(same, stats);
      expect(same.hashCode, stats.hashCode);
      final others = [
        const AudSfzVoiceStats(
          reserve: 4,
          sounding: 40,
          released: 12,
          stolen: 7,
          voiceFrames: 123456,
        ),
        const AudSfzVoiceStats(
          budget: 48,
          sounding: 40,
          released: 12,
          stolen: 7,
          voiceFrames: 123456,
        ),
        const AudSfzVoiceStats(
          budget: 48,
          reserve: 4,
          released: 12,
          stolen: 7,
          voiceFrames: 123456,
        ),
        const AudSfzVoiceStats(
          budget: 48,
          reserve: 4,
          sounding: 40,
          stolen: 7,
          voiceFrames: 123456,
        ),
        const AudSfzVoiceStats(
          budget: 48,
          reserve: 4,
          sounding: 40,
          released: 12,
          voiceFrames: 123456,
        ),
        const AudSfzVoiceStats(
          budget: 48,
          reserve: 4,
          sounding: 40,
          released: 12,
          stolen: 7,
        ),
      ];
      for (final other in others) {
        expect(other, isNot(stats));
      }
      expect(stats, isNot(Object()));
      expect(
        stats.toString(),
        'AudSfzVoiceStats({budget: 48, reserve: 4, sounding: 40, '
        'released: 12, stolen: 7, voiceFrames: 123456})',
      );
    });
  });
}

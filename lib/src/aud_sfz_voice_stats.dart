// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// #############################################################################
/// The voices of the sampler nodes of the process (ticket 29): the voice
/// budget with its reserve, the voices the live nodes published at the end
/// of their last block, and counters that only grow, from which a governor
/// derives the voices stolen and rendered per second.
///
/// ```dart
/// final before = AudDspSampler.voiceStats;
/// // ... 100 ms later
/// final now = AudDspSampler.voiceStats;
/// final stolen = now.stolen - before.stolen;
/// ```
class AudSfzVoiceStats {
  /// Creates the stats.
  const AudSfzVoiceStats({
    this.budget = 0,
    this.reserve = 0,
    this.sounding = 0,
    this.released = 0,
    this.stolen = 0,
    this.voiceFrames = 0,
  });

  // ...........................................................................
  /// The voice budget of the process; 0: none.
  final int budget;

  /// The free voices kept below the budget and below every `max_voices`.
  final int reserve;

  /// The voices that sound in the live nodes: they play and are neither
  /// turned off nor stolen.
  final int sounding;

  /// Of the [sounding] voices, those that are released: key and pedal up.
  final int released;

  /// The voices that `max_voices` and the budget stole so far, in all nodes.
  final int stolen;

  /// The voices rendered times the frames of their blocks so far, of the
  /// live blocks of all nodes.
  final int voiceFrames;

  // ...........................................................................
  /// The stats as JSON.
  Map<String, Object?> toJson() => {
    'budget': budget,
    'reserve': reserve,
    'sounding': sounding,
    'released': released,
    'stolen': stolen,
    'voiceFrames': voiceFrames,
  };

  @override
  bool operator ==(Object other) =>
      other is AudSfzVoiceStats &&
      other.budget == budget &&
      other.reserve == reserve &&
      other.sounding == sounding &&
      other.released == released &&
      other.stolen == stolen &&
      other.voiceFrames == voiceFrames;

  @override
  int get hashCode =>
      Object.hash(budget, reserve, sounding, released, stolen, voiceFrames);

  @override
  String toString() => 'AudSfzVoiceStats(${toJson()})';
}

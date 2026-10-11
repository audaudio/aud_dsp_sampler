// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:record_use/record_use.dart';

// Keeps the native library of the sampler - sfizz with the node - in a
// profile or release build only when the app uses it (interop-001, ticket
// 25): the members of AudDspSampler, the only Dart code that calls into the
// library, are recorded with @RecordUse. Without recordings the library
// stays, since nothing says it is unused.
void main(List<String> args) async {
  await link(args, (input, output) async {
    // The recordings are a dependency: the hooks runner caches a link by
    // the recordings of the build that ran it first, so a build whose
    // recordings live elsewhere - other defines, another flavor - would
    // take the old answer.
    // ignore: deprecated_member_use
    final recordings = input.recordedUsagesFile;
    if (recordings != null) output.dependencies.add(recordings);
    if (usesSampler(input.recordedUses)) {
      output.assets.code.addAll(input.assets.code);
    }
  });
}

const _sampler = Class(
  'AudDspSampler',
  Library('package:aud_dsp_sampler/src/aud_dsp_sampler_ffi.dart'),
);

/// The members of the package that call into its native library.
const List<DefinitionWithStaticCalls> samplerMembers = [
  Getter('register', _sampler),
  Method('registerWith', _sampler),
  Getter('storeBytes', _sampler),
  Getter('storeFiles', _sampler),
];

/// Whether an app with the recorded [uses] needs the native library.
bool usesSampler(Recordings? uses) =>
    uses == null ||
    samplerMembers.any((member) => (uses.calls[member] ?? const []).isNotEmpty);

#!/usr/bin/env python3
# @license
# Copyright (c) Audanika. All Rights Reserved.
#
# Use of this source code is governed by terms that can be
# found in the LICENSE file in the root of this package.

"""Vendors the subset of the sfizz fork that the sampler node needs.

The subset is what the linker pulls: build the fork with its own CMake for
every architecture family you ship (arm64 and x86_64), link a probe program
against the static libraries with a linker map per family, and point this
script at the checkout, the build directories and the maps. It lists the
translation units of the pulled objects, collects their header closure with
`clang -MM` over every build (platform-specific headers included), copies
the files with the license files into the destination and writes
SOURCES.txt and INCLUDES.txt for the build hook (decision build-001).

Usage:
  vendor_sfizz.py --checkout <sfizz> --dest <package>/src/third_party/sfizz \
      --build <dir> [--build <dir> ...] --map <linker map> [--map ...]

Linker maps: `-Wl,-map,<file>` with Apple's ld, `-Wl,-Map,<file>` with lld.
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

LIB_TO_TARGET = {
    'libsfizz.a': 'sfizz_static',
    'libsfizz_internal.a': 'sfizz_internal',
    'libsfizz_parser.a': 'sfizz_parser',
    'libsfizz_messaging.a': 'sfizz_messaging',
    'libsfizz_spin_mutex.a': 'sfizz_spin_mutex',
    'libsfizz_cephes.a': 'sfizz_cephes',
    'libsfizz_cpuid.a': 'sfizz_cpuid',
    'libsfizz_filesystem_impl.a': 'sfizz_filesystem_impl',
    'libsfizz_kissfft.a': 'sfizz_kissfft',
    'libsfizz_pugixml.a': 'sfizz_pugixml',
    'libsfizz_spline.a': 'sfizz_spline',
    'libsfizz_tunings.a': 'sfizz_tunings',
    'libst_audiofile.a': 'st_audiofile',
    'libst_audiofile_formats.a': 'st_audiofile_formats',
    'libwavpack.a': 'wavpack',
    'libaiff.a': 'aiff',
}

LICENSES = [
    'LICENSE',
    'external/abseil-cpp/LICENSE',
    'external/atomic_queue/LICENSE',
    'external/filesystem/LICENSE',
    'external/simde/COPYING',
    'external/st_audiofile/LICENSE.md',
    'external/st_audiofile/thirdparty/dr_libs/LICENSE',
    'external/st_audiofile/thirdparty/wavpack/COPYING',
    'external/st_audiofile/thirdparty/libaiff/LICENSE',
    'external/cephes/LICENSE.txt',
    'external/jsl/LICENSE.md',
    'external/invoke.hpp/LICENSE.md',
    'src/external/kiss_fft/COPYING',
    'src/external/pugixml/LICENSE.md',
    'src/external/spline/LICENSE',
    'src/external/tunings/LICENSE.md',
    'src/external/tunings/README.txt',
    'src/external/cpuid/LICENSE.rst',
    'src/external/hiir/license.txt',
]


def load_commands(build):
    commands = json.load(open(os.path.join(build, 'compile_commands.json')))
    for entry in commands:
        if 'output' not in entry:  # CMake before 3.27 writes no output key
            args = shlex.split(entry['command'])
            entry['output'] = args[args.index('-o') + 1]
        entry['source'] = os.path.realpath(
            os.path.join(entry['directory'], entry['file']))
    return commands


def pulled_objects(maps):
    pulled = {}
    for path in maps:
        for line in open(path):
            match = re.search(r'(lib[a-z_0-9]+\.a)\(([^)]+)\)', line)
            if match:
                pulled.setdefault(match.group(1), set()).add(match.group(2))
    return pulled


def sources_of(pulled, commands):
    sources = set()
    for lib, objects in sorted(pulled.items()):
        target = LIB_TO_TARGET.get(lib, lib[len('lib'):-2])
        for obj in sorted(objects):
            matches = [e for e in commands
                       if e['output'].endswith('/' + obj)
                       and '/' + target + '.dir/' in e['output']]
            if not matches and not obj.endswith('.S.o'):
                print('no source for', lib, obj, file=sys.stderr)
            sources.update(e['source'] for e in matches)
    return sources


def closure(checkout, builds, sources):
    files = set(sources)
    includes = set()
    for build in builds:
        for entry in load_commands(build):
            if entry['source'] not in sources:
                continue
            args = shlex.split(entry['command'])
            kept = []
            skip = False
            for arg in args[1:]:
                if skip:
                    skip = False
                    continue
                if arg == '-o':
                    skip = True
                    continue
                if arg == '-c':
                    continue
                if arg.startswith('-I'):
                    includes.add(os.path.relpath(
                        os.path.realpath(arg[2:]), checkout))
                kept.append(arg)
            result = subprocess.run([args[0]] + kept + ['-MM'],
                                    cwd=entry['directory'],
                                    capture_output=True, text=True)
            if result.returncode != 0:
                print('dependencies failed:', entry['source'],
                      result.stderr[:200], file=sys.stderr)
                continue
            deps = result.stdout.replace('\\\n', ' ').split(':', 1)[1].split()
            for dep in deps:
                dep = os.path.realpath(os.path.join(entry['directory'], dep))
                if dep.startswith(checkout + '/'):
                    files.add(dep)
    return files, includes


def relative(checkout, path):
    rel = os.path.relpath(path, checkout)
    # A generated translation unit of a build directory lands at the root.
    return os.path.basename(rel) if rel.startswith('build') else rel


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--checkout', required=True)
    parser.add_argument('--dest', required=True)
    parser.add_argument('--build', action='append', required=True)
    parser.add_argument('--map', action='append', required=True)
    args = parser.parse_args()
    checkout = os.path.realpath(args.checkout)
    commands = load_commands(args.build[0])
    sources = sources_of(pulled_objects(args.map), commands)
    files, includes = closure(checkout, args.build, sources)
    print(f'{len(sources)} sources, {len(files)} files', file=sys.stderr)
    if os.path.isdir(args.dest):
        shutil.rmtree(args.dest)
    for path in sorted(files):
        target = os.path.join(args.dest, relative(checkout, path))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(path, target)
    for license_file in LICENSES:
        source = os.path.join(checkout, license_file)
        if not os.path.exists(source):
            print('no license file', license_file, file=sys.stderr)
            continue
        target = os.path.join(args.dest, license_file)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(source, target)
    with open(os.path.join(args.dest, 'SOURCES.txt'), 'w') as out:
        out.write('\n'.join(sorted(relative(checkout, s) for s in sources)))
        out.write('\n')
    with open(os.path.join(args.dest, 'INCLUDES.txt'), 'w') as out:
        out.write('\n'.join(sorted(includes)) + '\n')


if __name__ == '__main__':
    main()

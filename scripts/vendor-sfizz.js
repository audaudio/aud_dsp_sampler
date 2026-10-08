#!/usr/bin/env node
/*
 * @license
 * Copyright (c) Audanika. All Rights Reserved.
 *
 * Use of this source code is governed by terms that can be
 * found in the LICENSE file in the root of this package.
 */

// Vendors the subset of the sfizz fork that the sampler node needs.
//
// The subset is what the linker pulls: build the fork with its own CMake for
// every architecture family you ship (arm64 and x86_64), link a probe program
// against the static libraries with a linker map per family, and point this
// script at the checkout, the build directories and the maps. It lists the
// translation units of the pulled objects, collects their header closure with
// `clang -MM` over every build (platform-specific headers included), copies
// the files with the license files into the destination and writes
// SOURCES.txt and INCLUDES.txt for the build hook (decision build-001).
//
// Usage:
//   node scripts/vendor-sfizz.js --checkout <sfizz> \
//     --dest src/third_party/sfizz \
//     --build <dir> [--build <dir> ...] --map <linker map> [--map ...]
//
// Linker maps: `-Wl,-map,<file>` with Apple's ld, `-Wl,-Map,<file>` with lld.

import { execFileSync } from 'node:child_process';
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  realpathSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { basename, dirname, join, relative, resolve } from 'node:path';

// ...........................................................................
// The static library of sfizz's build that holds each CMake target.
const libToTarget = {
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
};

// The license files that travel with the vendored sources.
const licenses = [
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
];

// ...........................................................................
// Splits a command line the way a POSIX shell does: whitespace separates,
// quotes group, a backslash escapes the next character.
function splitCommand(command) {
  const args = [];
  let current = '';
  let inArg = false;
  let quote = null;
  for (let i = 0; i < command.length; i++) {
    const char = command[i];
    if (quote) {
      if (char === quote) {
        quote = null;
      } else if (char === '\\' && quote === '"' && i + 1 < command.length) {
        current += command[++i];
      } else {
        current += char;
      }
    } else if (char === '"' || char === "'") {
      quote = char;
      inArg = true;
    } else if (char === '\\' && i + 1 < command.length) {
      current += command[++i];
      inArg = true;
    } else if (/\s/.test(char)) {
      if (inArg) args.push(current);
      current = '';
      inArg = false;
    } else {
      current += char;
      inArg = true;
    }
  }
  if (inArg) args.push(current);
  return args;
}

// ...........................................................................
// The real path of a file; a file that does not exist (a generated unit of
// a target that was not built) keeps its name under its real directory.
function realPath(path) {
  try {
    return realpathSync(path);
  } catch {
    return join(realPath(dirname(path)), basename(path));
  }
}

// Reads a build's compile_commands.json with the object path and the real
// source path of every entry.
function loadCommands(build) {
  const commands = JSON.parse(
    readFileSync(join(build, 'compile_commands.json'), 'utf8'),
  );
  for (const entry of commands) {
    entry.args = splitCommand(entry.command);
    if (!entry.output) {
      // CMake before 3.27 writes no output key.
      entry.output = entry.args[entry.args.indexOf('-o') + 1];
    }
    entry.source = realPath(resolve(entry.directory, entry.file));
  }
  return commands;
}

// The object files the linker pulled, per static library.
function pulledObjects(maps) {
  const pulled = new Map();
  for (const path of maps) {
    for (const line of readFileSync(path, 'utf8').split('\n')) {
      const match = line.match(/(lib[a-z_0-9]+\.a)\(([^)]+)\)/);
      if (!match) continue;
      if (!pulled.has(match[1])) pulled.set(match[1], new Set());
      pulled.get(match[1]).add(match[2]);
    }
  }
  return pulled;
}

// The source files behind the pulled objects.
function sourcesOf(pulled, commands) {
  const sources = new Set();
  for (const [lib, objects] of [...pulled.entries()].sort()) {
    const target = libToTarget[lib] ?? lib.slice('lib'.length, -'.a'.length);
    for (const object of [...objects].sort()) {
      const matches = commands.filter(
        (entry) =>
          entry.output.endsWith(`/${object}`) &&
          entry.output.includes(`/${target}.dir/`),
      );
      if (matches.length === 0 && !object.endsWith('.S.o')) {
        console.error(`no source for ${lib} ${object}`);
      }
      for (const entry of matches) sources.add(entry.source);
    }
  }
  return sources;
}

// The header closure of the sources over every build, and the include
// directories the builds use, relative to the checkout.
function closure(checkout, builds, sources) {
  const files = new Set(sources);
  const includes = new Set();
  for (const build of builds) {
    for (const entry of loadCommands(build)) {
      if (!sources.has(entry.source)) continue;
      const kept = [];
      let skip = false;
      for (const arg of entry.args.slice(1)) {
        if (skip) {
          skip = false;
          continue;
        }
        if (arg === '-o') {
          skip = true;
          continue;
        }
        if (arg === '-c') continue;
        if (arg.startsWith('-I')) {
          includes.add(relative(checkout, realPath(arg.slice(2))));
        }
        kept.push(arg);
      }
      let output;
      try {
        output = execFileSync(entry.args[0], [...kept, '-MM'], {
          cwd: entry.directory,
          encoding: 'utf8',
          stdio: ['ignore', 'pipe', 'pipe'],
        });
      } catch (error) {
        const reason = String(error.stderr ?? error.message).slice(0, 200);
        console.error(`dependencies failed: ${entry.source} ${reason}`);
        continue;
      }
      const deps = output
        .slice(output.indexOf(':') + 1)
        .replace(/\\\n/g, ' ')
        .trim()
        .split(/\s+/);
      for (const dep of deps) {
        const path = realPath(resolve(entry.directory, dep));
        if (path.startsWith(`${checkout}/`)) files.add(path);
      }
    }
  }
  return { files, includes };
}

// The path of a file inside the vendored tree. A generated translation unit
// of a build directory lands at the root.
function vendoredPath(checkout, path) {
  const rel = relative(checkout, path);
  return rel.startsWith('build') ? basename(rel) : rel;
}

// ...........................................................................
function parseArguments(argv) {
  const options = { build: [], map: [] };
  for (let i = 0; i < argv.length; i++) {
    const key = argv[i];
    const value = argv[i + 1];
    if (!key.startsWith('--') || value === undefined) {
      throw new Error(`Unexpected argument ${key}`);
    }
    i++;
    if (key === '--build' || key === '--map') {
      options[key.slice(2)].push(value);
    } else if (key === '--checkout' || key === '--dest') {
      options[key.slice(2)] = value;
    } else {
      throw new Error(`Unknown option ${key}`);
    }
  }
  for (const required of ['checkout', 'dest']) {
    if (!options[required]) throw new Error(`--${required} is required`);
  }
  if (options.build.length === 0 || options.map.length === 0) {
    throw new Error('--build and --map are required');
  }
  return options;
}

function copyInto(source, target) {
  mkdirSync(dirname(target), { recursive: true });
  copyFileSync(source, target);
}

function main() {
  const options = parseArguments(process.argv.slice(2));
  const checkout = realpathSync(options.checkout);
  const commands = loadCommands(options.build[0]);
  const sources = sourcesOf(pulledObjects(options.map), commands);
  const { files, includes } = closure(checkout, options.build, sources);
  console.error(`${sources.size} sources, ${files.size} files`);
  if (existsSync(options.dest)) rmSync(options.dest, { recursive: true });
  for (const path of [...files].sort()) {
    copyInto(path, join(options.dest, vendoredPath(checkout, path)));
  }
  for (const license of licenses) {
    const source = join(checkout, license);
    if (!existsSync(source)) {
      console.error(`no license file ${license}`);
      continue;
    }
    copyInto(source, join(options.dest, license));
  }
  const sourceList = [...sources].map((s) => vendoredPath(checkout, s)).sort();
  writeFileSync(join(options.dest, 'SOURCES.txt'), `${sourceList.join('\n')}\n`);
  writeFileSync(
    join(options.dest, 'INCLUDES.txt'),
    `${[...includes].sort().join('\n')}\n`,
  );
}

main();

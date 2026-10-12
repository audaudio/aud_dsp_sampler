// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Builds and runs the native tests of the sampler (ticket 25): the node and
// the package's fork of sfizz together with the graph engine of
// aud_audio_graph, all with the engine's debug watchdog, which then sees
// sfizz's allocations too. Up to three builds, like the graph's tests:
//
// - the address and the undefined behaviour sanitizer,
// - Clang's RealtimeSanitizer when a compiler on this machine has it, with
//   a probe that proves the run catches an allocation on the audio thread,
// - the thread sanitizer for the workers that load and stream.
//
// Every translation unit compiles to an object cached under .dart_tool by
// the hash of its flags, its source and the headers, in parallel; a run
// without changes only links and executes. Each build keeps only the
// objects and the binary of its last run. The options are in `usage` below.

'use strict';

const { createHash } = require('node:crypto');
const { spawn, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.resolve(__dirname, '..');

// The builds --only= names.
const variants = ['asan', 'plain', 'rtsan', 'tsan'];

const usage = `Usage: node scripts/test-native.js [options]

  (no options)      every build this machine has
  --no-sanitize     one build, no sanitizer
  --rtsan           fail without rtsan
  --no-rtsan        skip the rtsan build
  --no-tsan         skip the thread sanitizer
  --only=<build>    one build: ${variants.join(', ')}
  --help            this text

  AUD_TEST_FILTER=<part of a name>     run the matching tests only
  AUD_SFIZZ_PRISTINE=<sfizz checkout>  compare the RBJ filters of the fork
                                       with the Faust originals
`;

// The options of the command line; anything else ends the run with the
// usage and exit code 2.
function parseArgs(argv) {
  const options = {
    sanitize: true,
    wantRtsan: false,
    noRtsan: false,
    noTsan: false,
    only: '',
  };
  for (const arg of argv) {
    if (arg === '--help') {
      process.stdout.write(usage);
      process.exit(0);
    } else if (arg === '--no-sanitize') {
      options.sanitize = false;
    } else if (arg === '--rtsan') {
      options.wantRtsan = true;
    } else if (arg === '--no-rtsan') {
      options.noRtsan = true;
    } else if (arg === '--no-tsan') {
      options.noTsan = true;
    } else if (arg.startsWith('--only=') && variants.includes(arg.slice(7))) {
      options.only = arg.slice(7);
    } else {
      process.stderr.write(`Unknown option ${arg}\n\n${usage}`);
      process.exit(2);
    }
  }
  return options;
}

const options = parseArgs(process.argv.slice(2));
const { sanitize, wantRtsan, only } = options;
const skipRtsan =
  options.noRtsan || !sanitize || (only !== '' && only !== 'rtsan');
const skipTsan =
  options.noTsan || !sanitize || (only !== '' && only !== 'tsan');
const skipMain = only !== '' && only !== 'asan' && only !== 'plain';

// Compilers that may carry the RealtimeSanitizer: Apple's clang refuses
// it on arm64, LLVM from Homebrew has it.
const rtsanCandidates = [
  process.env.AUD_RTSAN_CXX,
  '/opt/homebrew/opt/llvm/bin/clang++',
  '/usr/local/opt/llvm/bin/clang++',
  'clang++',
].filter(Boolean);

// The defines the build hook passes for sfizz. The hook builds in release
// mode, where CBuilder defines NDEBUG: sfizz's debug messages, which write
// to stderr on the audio thread, are gone. The tests keep sfizz's asserts.
const sfizzDefines = [
  '-DGHC_FILESYSTEM_FWD',
  '-DENABLE_DSD',
  '-DENABLE_THREADS',
  '-DHAVE_FSEEKO',
  '-DHAVE___BUILTIN_CLZ',
  '-DNOMINMAX',
  '-DNDEBUG',
  '-DSFIZZ_ENABLE_RELEASE_ASSERT',
];

// The root directory of a package, resolved through the package config.
function packageRoot(name) {
  const configPath = path.join(root, '.dart_tool', 'package_config.json');
  if (!fs.existsSync(configPath)) {
    throw new Error(`Run dart pub get first: ${configPath} is missing`);
  }
  const config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  const entry = config.packages.find((p) => p.name === name);
  if (!entry) throw new Error(`Package ${name} is not in the package config`);
  const rootUri = new URL(entry.rootUri, `file://${configPath}`);
  return decodeURIComponent(rootUri.pathname);
}

function lines(file) {
  return fs
    .readFileSync(file, 'utf8')
    .split('\n')
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
}

function listFiles(directory, extensions) {
  return fs
    .readdirSync(directory)
    .filter((file) => extensions.some((extension) => file.endsWith(extension)))
    .map((file) => path.join(directory, file))
    .sort();
}

// Every header below the include directories, recursively.
function headersBelow(directories) {
  const result = [];
  const visit = (directory) => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        visit(file);
      } else if (/\.(h|hh|hpp|hxx|inc|ipp)$/.test(entry.name)) {
        result.push(file);
      }
    }
  };
  for (const directory of directories) visit(directory);
  return [...new Set(result)].sort();
}

// Whether `compiler` exists and builds a program with `flags`.
function works(compiler, flags) {
  const probe = spawnSync(compiler, ['--version'], { encoding: 'utf8' });
  if (probe.status !== 0) return false;
  if (flags.length === 0) return true;
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'aud-probe-'));
  try {
    const source = path.join(dir, 'probe.cpp');
    fs.writeFileSync(source, 'int main() { return 0; }\n');
    const build = spawnSync(
      compiler,
      [...flags, source, '-o', path.join(dir, 'probe')],
      { encoding: 'utf8' },
    );
    return build.status === 0;
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

function rtsanCompiler() {
  for (const candidate of rtsanCandidates) {
    if (works(candidate, ['-fsanitize=realtime'])) return candidate;
  }
  return null;
}

// The units to compile with their own flags: the graph engine as C++17 with
// the watchdog, the node as C++14 like the build hook compiles it on Apple,
// sfizz's C++ as gnu++14 and its C as gnu11, the tests as C++17. The tests
// of the fork, sfizz_*_test.cpp, compile like the fork: sfizz's types
// depend on the language version - std::filesystem or ghc's, Abseil's
// string_view and optional or the standard's -, and a test must see them
// as sfizz does.
function units(context) {
  const graphFlags = ['-std=c++17', '-DAUD_GRAPH_WATCHDOG=1'];
  if (context.rtsan) graphFlags.push('-DAUD_GRAPH_RTSAN=1');
  const result = [];
  for (const file of context.graphSources) {
    result.push({ file, flags: graphFlags });
  }
  for (const file of context.samplerSources) {
    result.push({ file, flags: ['-std=c++14', ...sfizzDefines] });
  }
  for (const file of context.sfizzSources) {
    // clang++ takes a .c file for C++ unless it is told the language.
    // sfizz's SIMD code hands vector pointers to unaligned floats to the
    // unaligned-load intrinsics; UBSan's alignment check flags the pointer
    // type, although the loads take any alignment.
    const cpp = /\.(cc|cpp|cxx)$/.test(file);
    result.push({
      file,
      flags: [
        ...(cpp ? ['-std=gnu++14'] : ['-x', 'c', '-std=gnu11']),
        ...sfizzDefines,
        '-fno-sanitize=alignment',
        '-w',
      ],
    });
  }
  for (const file of context.testSources) {
    const fork = path.basename(file).startsWith('sfizz_');
    const flags = [fork ? '-std=gnu++14' : '-std=c++17', ...sfizzDefines];
    if (context.pristineFilters && file.endsWith('sfizz_rbj_filters_test.cpp')) {
      // A C string literal of the path: JSON escapes quotes and backslashes
      // the way C does.
      flags.push(
        `-DSFIZZ_PRISTINE_FILTER_IMPLS=${JSON.stringify(context.pristineFilters)}`,
      );
    }
    result.push({ file, flags });
  }
  return result;
}

function runCompiler(compiler, flags) {
  return new Promise((resolve) => {
    const child = spawn(compiler, flags, { stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    child.stdout.on('data', (data) => (output += data));
    child.stderr.on('data', (data) => (output += data));
    child.on('close', (status) => resolve({ status, output }));
  });
}

// The folder of the objects and the binary of a build.
function outDirOf(name) {
  return path.join(root, '.dart_tool', 'aud_native_test', name);
}

// Compiles what is not cached, at most one unit per core at a time; returns
// the objects.
async function compileAll(name, compiler, variantFlags, context) {
  const outDir = outDirOf(name);
  fs.mkdirSync(outDir, { recursive: true });
  const common = [
    '-c',
    '-g',
    '-O1',
    '-fno-omit-frame-pointer',
    ...variantFlags,
    ...context.includes.map((directory) => `-I${directory}`),
    // sfizz and its externals warn in headers the node includes.
    ...context.systemIncludes.flatMap((directory) => ['-isystem', directory]),
  ];
  if (process.arch === 'x64') common.push('-mavx');
  const jobs = units(context).map(({ file, flags }) => {
    const all = [...common, ...flags];
    const hash = createHash('sha256');
    hash.update(compiler);
    hash.update(all.join(' '));
    hash.update(context.headersHash);
    hash.update(fs.readFileSync(file));
    const object = path.join(
      outDir,
      `${path.basename(file)}-${hash.digest('hex').slice(0, 16)}.o`,
    );
    return { file, object, flags: [...all, file, '-o', object] };
  });
  const pending = jobs.filter((job) => !fs.existsSync(job.object));
  if (pending.length > 0) {
    process.stdout.write(`${name}: compiling ${pending.length} units\n`);
  }
  let next = 0;
  let failed = null;
  const worker = async () => {
    while (next < pending.length && failed === null) {
      const job = pending[next++];
      const result = await runCompiler(compiler, job.flags);
      if (result.status !== 0) {
        failed = `${job.file}\n${result.output}`;
      } else if (result.output.trim().length > 0 && !job.flags.includes('-w')) {
        process.stdout.write(result.output);
      }
    }
  };
  const parallel = Math.max(1, os.cpus().length);
  await Promise.all(Array.from({ length: parallel }, worker));
  if (failed !== null) {
    process.stderr.write(`${name}: compiling failed: ${failed}\n`);
    process.exit(1);
  }
  return jobs.map((job) => job.object);
}

async function build(name, compiler, variantFlags, context) {
  const objects = await compileAll(name, compiler, variantFlags, context);
  const hash = createHash('sha256');
  for (const object of objects) hash.update(object);
  const outDir = outDirOf(name);
  const binary = path.join(
    outDir,
    `aud_sampler_test-${hash.digest('hex').slice(0, 16)}`,
  );
  if (!fs.existsSync(binary)) {
    const flags = [...variantFlags, ...objects, '-o', binary];
    if (process.platform !== 'win32') flags.push('-lpthread');
    const result = spawnSync(compiler, flags, { encoding: 'utf8' });
    if (result.status !== 0) {
      process.stderr.write(result.stdout + result.stderr);
      process.exit(result.status ?? 1);
    }
  }
  prune(outDir, [...objects, binary]);
  return binary;
}

// Removes what earlier runs left in the folder of a build: the objects and
// binaries of sources, headers or flags that changed since.
function prune(outDir, keep) {
  const kept = new Set(keep.map((file) => path.basename(file)));
  for (const entry of fs.readdirSync(outDir)) {
    if (kept.has(entry)) continue;
    fs.rmSync(path.join(outDir, entry), { recursive: true, force: true });
  }
}

function execute(binary, env) {
  return spawnSync(binary, [], {
    encoding: 'utf8',
    cwd: root,
    env: { ...process.env, ...env },
    maxBuffer: 64 * 1024 * 1024,
  });
}

function run(name, binary, env) {
  process.stdout.write(`${name}: ${path.basename(binary)}\n`);
  const result = execute(binary, env);
  process.stdout.write(result.stdout);
  process.stderr.write(result.stderr);
  if (result.status !== 0) process.exit(result.status ?? 1);
}

// The rtsan run must catch an allocation on the audio thread once the probe
// node stops telling the sanitizer; otherwise its silence would prove
// nothing.
function probeRtsan(binary, env) {
  const result = execute(binary, {
    ...env,
    AUD_TEST_FILTER: 'rtsan_probe',
    AUD_TEST_RTSAN_PROBE: '1',
  });
  if (result.status === 0 || !result.stderr.includes('RealtimeSanitizer')) {
    process.stderr.write(
      'rtsan: the probe allocation on the audio thread was not caught\n' +
        result.stdout +
        result.stderr,
    );
    process.exit(1);
  }
  process.stdout.write('rtsan: the probe allocation was caught\n');
}

// The filters of a pristine sfizz checkout the RBJ filters are compared
// with, or null.
function pristineFilters() {
  const checkout = process.env.AUD_SFIZZ_PRISTINE;
  if (!checkout) return null;
  const file = path.join(path.resolve(checkout), 'src', 'sfizz', 'SfzFilterImpls.hpp');
  if (!fs.existsSync(file)) throw new Error(`${file} is missing`);
  return file;
}

async function main() {
  const coreSrc = path.join(packageRoot('aud_audio_core'), 'src');
  const graphSrc = path.join(packageRoot('aud_audio_graph'), 'src');
  const sfizz = path.join(root, 'src', 'third_party', 'sfizz');
  const sfizzIncludes = lines(path.join(sfizz, 'INCLUDES.txt')).map((d) =>
    path.join(sfizz, d),
  );
  const includes = [
    path.join(root, 'src'),
    coreSrc,
    graphSrc,
    path.join(root, 'test', 'native'),
  ];
  const headers = headersBelow([
    path.join(root, 'src'),
    coreSrc,
    graphSrc,
    path.join(sfizz, 'src'),
    path.join(sfizz, 'external'),
    path.join(root, 'test', 'native'),
  ]);
  const headersHash = createHash('sha256');
  for (const header of headers) {
    headersHash.update(header);
    headersHash.update(fs.readFileSync(header));
  }
  const context = {
    includes,
    systemIncludes: sfizzIncludes,
    headersHash: headersHash.digest('hex'),
    graphSources: listFiles(graphSrc, ['.cpp']),
    samplerSources: listFiles(path.join(root, 'src'), ['.cpp']),
    sfizzSources: lines(path.join(sfizz, 'SOURCES.txt')).map((file) =>
      path.join(sfizz, file),
    ),
    // The tests of the package and of its fork of sfizz.
    testSources: listFiles(path.join(root, 'test', 'native'), ['.cpp']),
    pristineFilters: pristineFilters(),
    rtsan: false,
  };

  const compiler = 'clang++';
  if (!works(compiler, [])) throw new Error('clang++ is missing');
  // The leak check exists on Linux only; Apple's runtime refuses the option.
  const leaks =
    process.platform === 'linux' ? 'detect_leaks=1' : 'detect_leaks=0';
  const plain = !sanitize || only === 'plain';
  if (!skipMain) {
    const main = await build(
      plain ? 'plain' : 'asan',
      compiler,
      plain ? [] : ['-fsanitize=address,undefined'],
      context,
    );
    run(plain ? 'plain' : 'asan+ubsan', main, {
      UBSAN_OPTIONS: 'print_stacktrace=1:halt_on_error=1',
      ASAN_OPTIONS: leaks,
    });
  }
  if (!skipRtsan) {
    const rtsanCxx = rtsanCompiler();
    if (rtsanCxx === null) {
      if (wantRtsan) {
        throw new Error('No compiler with -fsanitize=realtime found');
      }
      process.stdout.write('rtsan: no compiler has it, skipped\n');
    } else {
      const rtsan = await build(
        'rtsan',
        rtsanCxx,
        ['-fsanitize=realtime', '-Wno-function-effects'],
        { ...context, rtsan: true },
      );
      // Every violation aborts the run: the tests fail on any.
      const env = { RTSAN_OPTIONS: 'halt_on_error=true' };
      run('rtsan', rtsan, env);
      probeRtsan(rtsan, env);
    }
  }
  if (!skipTsan) {
    if (!works(compiler, ['-fsanitize=thread'])) {
      process.stdout.write('tsan: the compiler lacks it, skipped\n');
      return;
    }
    const tsan = await build('tsan', compiler, ['-fsanitize=thread'], context);
    run('tsan', tsan, {
      TSAN_OPTIONS: 'halt_on_error=1:second_deadlock_stack=1',
    });
  }
}

main().catch((error) => {
  process.stderr.write(`${error.stack ?? error}\n`);
  process.exit(1);
});

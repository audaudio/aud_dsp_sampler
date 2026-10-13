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
//
// --bench builds the micro-benchmark of ticket 28 instead
// (test/native/bench/sfizz_bench.cpp) twice at -O3, like the build hook:
// with the fork of sfizz of a release before the change and with the fork of
// now. It prints what the hot spots of an app's audio thread cost per call
// before and after, and compares the renders of the app's instrument of
// both builds.

'use strict';

const { createHash } = require('node:crypto');
const { spawn, spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.resolve(__dirname, '..');

// The builds --only= names.
const variants = ['asan', 'plain', 'rtsan', 'tsan'];

// The release whose fork of sfizz --bench measures as before: the last one
// before ticket 28 made the hot spots of the audio thread faster.
const benchBaseDefault = '0.3.0';

const usage = `Usage: node scripts/test-native.js [options]

  (no options)      every build this machine has
  --no-sanitize     one build, no sanitizer
  --rtsan           fail without rtsan
  --no-rtsan        skip the rtsan build
  --no-tsan         skip the thread sanitizer
  --only=<build>    one build: ${variants.join(', ')}
  --bench           the benchmark of the fork before and after ticket 28
  --help            this text

  AUD_TEST_FILTER=<part of a name>     run the matching tests only
  AUD_SFIZZ_PRISTINE=<sfizz checkout>  compare the RBJ filters of the fork
                                       with the Faust originals
  AUD_RTSAN_CXX=<clang++>              the compiler of the rtsan build
  AUD_LLVM_SYMBOLIZER=<llvm-symbolizer>
                                       the symbolizer of the tsan reports
                                       on macOS, beside LLVM's dsymutil
  AUD_BENCH_BASE=<git ref>             the fork --bench compares with,
                                       default ${benchBaseDefault}
  UPDATE_GOLDENS=1                     --bench writes the golden of the
                                       app's instrument with that fork
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
    bench: false,
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
    } else if (arg === '--bench') {
      options.bench = true;
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

// The llvm-symbolizers the thread sanitizer may report with on macOS, those
// of LLVM from Homebrew; on other systems it finds one itself.
const symbolizerCandidates = [
  process.env.AUD_LLVM_SYMBOLIZER,
  '/opt/homebrew/opt/llvm/bin/llvm-symbolizer',
  '/usr/local/opt/llvm/bin/llvm-symbolizer',
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

// The llvm-symbolizer the thread sanitizer reports with on macOS, or null.
// There it asks atos first, which hung on reports of races between threads
// that load (ticket 28) instead of letting the run fail.
function llvmSymbolizer() {
  if (process.platform !== 'darwin') return null;
  return symbolizerCandidates.find((file) => fs.existsSync(file)) ?? null;
}

// Writes the debug information of `binary` into a dSYM bundle beside it,
// from which llvm-symbolizer reads the files and lines of a report; LLVM's
// dsymutil lies next to the symbolizer. A failed run leaves no bundle, so
// the next run tries again.
function writeDsym(binary, symbolizer) {
  const bundle = `${binary}.dSYM`;
  if (fs.existsSync(bundle)) return;
  const dsymutil = path.join(path.dirname(symbolizer), 'dsymutil');
  const result = spawnSync(dsymutil, [binary, '-o', bundle], {
    encoding: 'utf8',
  });
  if (result.status !== 0) {
    fs.rmSync(bundle, { recursive: true, force: true });
    const reason = result.error?.message ?? result.stderr;
    process.stdout.write(
      `tsan: dsymutil failed, the reports name no lines\n${reason}\n`,
    );
  }
}

// The options of the thread sanitizer: every report fails the run.
function tsanOptions(symbolizer) {
  const options = 'halt_on_error=1:second_deadlock_stack=1';
  if (symbolizer === null) return options;
  return `${options}:external_symbolizer_path=${symbolizer}`;
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
  const defines = context.defines ?? sfizzDefines;
  for (const file of context.samplerSources) {
    result.push({ file, flags: ['-std=c++14', ...defines] });
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
        ...defines,
        '-fno-sanitize=alignment',
        '-w',
      ],
    });
  }
  for (const file of context.testSources) {
    const fork = path.basename(file).startsWith('sfizz_');
    const flags = [fork ? '-std=gnu++14' : '-std=c++17', ...defines];
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
    context.optimization ?? '-O1',
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
  // The dSYM bundle of the binary, when the tsan run wrote one.
  prune(outDir, [...objects, binary, `${binary}.dSYM`]);
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

// ............................................................................
// The benchmark of ticket 28

// The fork of sfizz at the git ref `ref`, unpacked once below .dart_tool;
// the folders of other refs go.
function forkAt(ref) {
  const resolved = spawnSync('git', ['rev-parse', '--verify', `${ref}^{commit}`], {
    cwd: root,
    encoding: 'utf8',
  });
  if (resolved.status !== 0) throw new Error(`git cannot resolve ${ref}`);
  const commit = resolved.stdout.trim().slice(0, 12);
  const bases = path.join(root, '.dart_tool', 'aud_native_test', 'bench-base');
  const folder = path.join(bases, commit);
  const sfizz = path.join(folder, 'src', 'third_party', 'sfizz');
  if (!fs.existsSync(path.join(sfizz, 'SOURCES.txt'))) {
    fs.rmSync(bases, { recursive: true, force: true });
    fs.mkdirSync(folder, { recursive: true });
    const tar = path.join(folder, 'fork.tar');
    const steps = [
      ['git', ['archive', '-o', tar, commit, 'src/third_party/sfizz'], root],
      ['tar', ['-xf', tar, '-C', folder], folder],
    ];
    for (const [command, args, cwd] of steps) {
      const result = spawnSync(command, args, { cwd, encoding: 'utf8' });
      if (result.status !== 0) {
        fs.rmSync(folder, { recursive: true, force: true });
        throw new Error(`${command} ${args.join(' ')} failed: ${result.stderr}`);
      }
    }
    fs.rmSync(tar);
  }
  return sfizz;
}

// The units of the benchmark on the fork in `sfizz`, built like the hook
// builds them: -O3, NDEBUG, without sfizz's release asserts.
function benchContext(sfizz) {
  const testNative = path.join(root, 'test', 'native');
  const headers = headersBelow([
    path.join(sfizz, 'src'),
    path.join(sfizz, 'external'),
    testNative,
  ]);
  const headersHash = createHash('sha256');
  for (const header of headers) {
    headersHash.update(header);
    headersHash.update(fs.readFileSync(header));
  }
  return {
    includes: [testNative],
    systemIncludes: lines(path.join(sfizz, 'INCLUDES.txt')).map((d) =>
      path.join(sfizz, d),
    ),
    headersHash: headersHash.digest('hex'),
    graphSources: [],
    samplerSources: [],
    sfizzSources: lines(path.join(sfizz, 'SOURCES.txt')).map((file) =>
      path.join(sfizz, file),
    ),
    testSources: [path.join(testNative, 'bench', 'sfizz_bench.cpp')],
    pristineFilters: null,
    rtsan: false,
    defines: sfizzDefines.filter((d) => d !== '-DSFIZZ_ENABLE_RELEASE_ASSERT'),
    optimization: '-O3',
  };
}

// Runs a benchmark binary; returns its measurements by name. The renders
// land in `folder`.
function runBench(name, binary, folder) {
  fs.rmSync(folder, { recursive: true, force: true });
  fs.mkdirSync(folder, { recursive: true });
  process.stdout.write(`${name}: ${path.basename(binary)}\n`);
  const result = spawnSync(binary, [folder], {
    encoding: 'utf8',
    cwd: root,
    maxBuffer: 64 * 1024 * 1024,
  });
  if (result.status !== 0) {
    process.stderr.write(result.stdout + result.stderr);
    process.exit(result.status ?? 1);
  }
  const rows = new Map();
  for (const line of result.stdout.split('\n')) {
    if (!line.startsWith('AUD_BENCH ')) continue;
    const row = JSON.parse(line.slice('AUD_BENCH '.length));
    rows.set(row.name, row.ns);
  }
  return rows;
}

// The samples of a WAV file of 32-bit floats.
function wavSamples(file) {
  const bytes = fs.readFileSync(file);
  for (let at = 12; at + 8 <= bytes.length; ) {
    const size = bytes.readUInt32LE(at + 4);
    if (bytes.toString('latin1', at, at + 4) === 'data') {
      const samples = new Float32Array(size / 4);
      for (let i = 0; i < samples.length; i++) {
        samples[i] = bytes.readFloatLE(at + 8 + 4 * i);
      }
      return samples;
    }
    at += 8 + size + (size & 1);
  }
  throw new Error(`${file} has no data`);
}

// How two renders differ: the samples whose bits differ, the largest
// difference.
function renderDifference(a, b) {
  if (a.length !== b.length) return { bits: Infinity, largest: Infinity };
  const bitsA = new Uint32Array(a.buffer);
  const bitsB = new Uint32Array(b.buffer);
  let bits = 0;
  let largest = 0;
  for (let i = 0; i < a.length; i++) {
    if (bitsA[i] !== bitsB[i]) bits++;
    largest = Math.max(largest, Math.abs(a[i] - b[i]));
  }
  return { bits, largest };
}

function formatNs(ns) {
  if (ns >= 1e6) return `${(ns / 1e6).toFixed(2)} ms`;
  if (ns >= 1e3) return `${(ns / 1e3).toFixed(2)} µs`;
  return `${ns.toFixed(1)} ns`;
}

// Builds and runs the benchmark before and after, prints the table and
// compares the renders; fails when they differ by more than 1e-6.
async function bench() {
  const compiler = 'clang++';
  if (!works(compiler, [])) throw new Error('clang++ is missing');
  const base = process.env.AUD_BENCH_BASE || benchBaseDefault;
  const builds = [
    ['bench-before', forkAt(base)],
    ['bench-after', path.join(root, 'src', 'third_party', 'sfizz')],
  ];
  const results = [];
  for (const [name, sfizz] of builds) {
    const binary = await build(name, compiler, [], benchContext(sfizz));
    const folder = path.join(outDirOf(name), 'renders');
    results.push({ name, folder, rows: runBench(name, binary, folder) });
  }
  const [before, after] = results;
  const names = [...before.rows.keys()];
  const width = Math.max(...names.map((n) => n.length));
  const header = `${'per call'.padEnd(width)} | ${'before'.padStart(10)} | ${'after'.padStart(10)} | speedup`;
  process.stdout.write(`\nThe fork of ${base} (before) and of now (after), -O3:\n\n`);
  process.stdout.write(`${header}\n${'-'.repeat(header.length)}\n`);
  for (const name of names) {
    const a = before.rows.get(name);
    const b = after.rows.get(name);
    const speedup = a / b;
    process.stdout.write(
      `${name.padEnd(width)} | ${formatNs(a).padStart(10)} | ${formatNs(b).padStart(10)} | ` +
        `${speedup >= 10 ? speedup.toFixed(0) : speedup.toFixed(1)}x\n`,
    );
  }
  process.stdout.write('\n');
  let failed = false;
  for (const render of ['render-golden.wav', 'render-bend.wav']) {
    const difference = renderDifference(
      wavSamples(path.join(before.folder, render)),
      wavSamples(path.join(after.folder, render)),
    );
    process.stdout.write(
      `${render}: ${difference.bits} samples differ in their bits, ` +
        `by at most ${difference.largest}\n`,
    );
    if (!(difference.largest <= 1e-6)) failed = true;
  }
  if (process.env.UPDATE_GOLDENS === '1') {
    const golden = path.join(root, 'test', 'goldens', 'sfizz_app_instrument.wav');
    fs.copyFileSync(path.join(before.folder, 'render-golden.wav'), golden);
    process.stdout.write(`wrote ${path.relative(root, golden)} with the fork of ${base}\n`);
  }
  if (failed) {
    process.stderr.write('bench: the renders differ by more than 1e-6\n');
    process.exit(1);
  }
}

async function main() {
  if (options.bench) {
    await bench();
    return;
  }
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
    const symbolizer = llvmSymbolizer();
    if (symbolizer !== null) writeDsym(tsan, symbolizer);
    run('tsan', tsan, { TSAN_OPTIONS: tsanOptions(symbolizer) });
  }
}

main().catch((error) => {
  process.stderr.write(`${error.stack ?? error}\n`);
  process.exit(1);
});

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the RBJ biquads that replace the Faust-generated resonant, peaking
// and shelving filters of the sfizz fork (src/sfizz/RbjFilters.hpp), one
// test per filter and aspect, each for 1 and 2 channels:
//
// - parameters: the setters and getters of each filter and their defaults
// - magnitudes: the steady-state response to sines, with coefficient
//   smoothing off and on, against the Audio EQ Cookbook's analog prototype
//   through the bilinear transform
// - impulses: the first samples of impulse responses against the difference
//   equation of the cookbook coefficients, which climb from 0 with a time
//   constant of 1 ms, or start on their targets without smoothing and after
//   prepare(); also with parameters beyond their limits and NaN
// - originals: the output against the Faust-generated classes they replace,
//   for noise and a sweep under changing parameters. This part needs the
//   pristine sfizz sources, which are not part of this repository: it
//   compiles when SFIZZ_PRISTINE_FILTER_IMPLS names their
//   src/sfizz/SfzFilterImpls.hpp, and is left out otherwise.
//   scripts/test-native.js defines it when AUD_SFIZZ_PRISTINE names a
//   checkout of sfizz.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <vector>

#include "SfzFilterImpls.hpp"

#include "aud_test.hpp"

#if defined(SFIZZ_PRISTINE_FILTER_IMPLS)
#if __has_include(SFIZZ_PRISTINE_FILTER_IMPLS)
#define HAVE_ORIGINALS 1
// The original filters, with their own sfzFilterDsp. The generated headers
// that the fork still uses were included above, so their include guards
// skip them here: only the replaced classes come in a second time.
namespace original {
#include SFIZZ_PRISTINE_FILTER_IMPLS
}
#endif
#endif
#ifndef HAVE_ORIGINALS
#define HAVE_ORIGINALS 0
#endif

namespace {

enum class Shape { Lowpass, Highpass, Bandpass, Notch, Peaking, LowShelf, HighShelf };

// The parameters of a filter: cutoff and resonance, plus gain for peaks and
// shelves; cutoff, gain and bandwidth for equalizer bands.
enum class Kind { Resonant, PeakShelf, Eq };

// name, shape, sections, kind
#define RBJ_FILTERS(F)             \
  F(Lpf2p, Lowpass, 1, Resonant)   \
  F(Lpf4p, Lowpass, 2, Resonant)   \
  F(Lpf6p, Lowpass, 3, Resonant)   \
  F(Hpf2p, Highpass, 1, Resonant)  \
  F(Hpf4p, Highpass, 2, Resonant)  \
  F(Hpf6p, Highpass, 3, Resonant)  \
  F(Bpf2p, Bandpass, 1, Resonant)  \
  F(Bpf4p, Bandpass, 2, Resonant)  \
  F(Bpf6p, Bandpass, 3, Resonant)  \
  F(Brf2p, Notch, 1, Resonant)     \
  F(Lsh, LowShelf, 1, PeakShelf)   \
  F(Hsh, HighShelf, 1, PeakShelf)  \
  F(Peq, Peaking, 1, PeakShelf)    \
  F(EqPeak, Peaking, 1, Eq)        \
  F(EqLshelf, LowShelf, 1, Eq)     \
  F(EqHshelf, HighShelf, 1, Eq)

struct Design {
  Shape shape;
  int sections;
  Kind kind;
};

struct Settings {
  float cutoff;     // Hz
  float resonance;  // dB
  float gain;       // dB, of peaks and shelves
  float bandwidth;  // octaves, of equalizer bands
};

constexpr Settings kDefaults{440.0f, 0.0f, 0.0f, 1.0f};
constexpr double kPi = 3.141592653589793238462643383280;
// The largest relative deviation of a measured magnitude from the cookbook,
// and the absolute floor below which magnitudes count as equal.
constexpr double kMagnitudeTolerance = 1e-5;
constexpr double kMagnitudeFloor = 1e-7;
// The largest deviation of an impulse response from the difference
// equation, relative to its largest sample.
constexpr double kImpulseTolerance = 1e-6;
// The frames of the noise and the renders against the originals.
constexpr int kFrames = 24000;

bool operator==(const Settings& a, const Settings& b) {
  return a.cutoff == b.cutoff && a.resonance == b.resonance && a.gain == b.gain &&
         a.bandwidth == b.bandwidth;
}

// ............................................................................
// The filters as sfizz drives them

// Sets the parameters through the wrappers of SfzFilterImpls.hpp. Each
// filter takes the call that matches its kind; the other one does nothing.
template <class Dsp>
void configure(Dsp& dsp, const Settings& s) {
  dsp.configureStandard(s.cutoff, s.resonance, s.gain);
  dsp.configureEq(s.cutoff, s.bandwidth, s.gain);
}

// What sfz::Filter::prepare() does: start on the settings without smoothing.
template <class Dsp>
void prepare(Dsp& dsp, const Settings& s) {
  float buffer[2] = {};
  float* inout[2] = {&buffer[0], &buffer[1]};
  const bool smoothing = dsp.isSmoothingEnabled();
  dsp.instanceClear();
  configure(dsp, s);
  dsp.setSmoothingEnabled(false);
  dsp.compute(1, inout, inout);
  dsp.setSmoothingEnabled(smoothing);
}

// ............................................................................
// Parameters

// The parameters of a filter of kind K; those it lacks read as the defaults.
template <Kind K>
struct Parameters;

template <>
struct Parameters<Kind::Resonant> {
  template <class F>
  static Settings of(const F& f) {
    return {f.getCutoff(), f.getResonance(), kDefaults.gain, kDefaults.bandwidth};
  }
};

template <>
struct Parameters<Kind::PeakShelf> {
  template <class F>
  static Settings of(const F& f) {
    return {f.getCutoff(), f.getResonance(), f.getPeakShelfGain(), kDefaults.bandwidth};
  }
};

template <>
struct Parameters<Kind::Eq> {
  template <class F>
  static Settings of(const F& f) {
    return {f.getCutoff(), kDefaults.resonance, f.getPeakShelfGain(), f.getBandwidth()};
  }
};

// What a filter of kind K keeps of s.
template <Kind K>
Settings keptOf(Settings s) {
  if (K == Kind::Eq) s.resonance = kDefaults.resonance;
  if (K == Kind::Resonant) s.gain = kDefaults.gain;
  if (K != Kind::Eq) s.bandwidth = kDefaults.bandwidth;
  return s;
}

// The setters and getters of F, its defaults after init() and again after
// a second init().
template <class F, Kind K>
void checkParameters() {
  const Settings set{1234.5f, -7.25f, 3.5f, 0.75f};
  F f;
  f.init(48000);
  AUD_CHECK(Parameters<K>::of(f) == kDefaults);
  configure(f, set);
  AUD_CHECK(Parameters<K>::of(f) == keptOf<K>(set));
  f.init(44100);
  AUD_CHECK(Parameters<K>::of(f) == kDefaults);
}

// ............................................................................
// The reference: the cookbook's analog prototypes, which its bilinear
// transform s = (1 - z^-1) / ((1 + z^-1) K), K = tan(w0 / 2), turns into
// the biquads, with the parameter conventions of sfz_filters.dsp

// w0, Q and A of one section.
struct Reference {
  double w0;
  double q;
  double A;
};

// x within [lo, hi]; NaN counts as lo.
double limited(float x, double lo, double hi) {
  return std::isnan(x) ? lo : std::min(std::max(static_cast<double>(x), lo), hi);
}

// The settings as sfz_filters.dsp takes them: each parameter within its
// limits, the bandwidth as the cookbook's BW case, Q at least 0.001.
Reference reference(const Design& d, const Settings& s, int sampleRate) {
  Reference r;
  r.w0 = 2.0 * kPi * limited(s.cutoff, 1.0, 20000.0) / sampleRate;
  r.A = d.kind == Kind::Resonant ? 1.0 : std::pow(10.0, limited(s.gain, -120.0, 60.0) / 40.0);
  if (d.kind == Kind::Eq) {
    const double bw = limited(s.bandwidth, 0.01, 12.0);
    const double alpha =
        std::sin(r.w0) * std::sinh(std::log(2.0) / 2.0 * bw * r.w0 / std::sin(r.w0));
    r.q = std::sin(r.w0) / (2.0 * alpha);
  } else {
    r.q = std::pow(10.0, limited(s.resonance, -60.0, 60.0) / 20.0);
  }
  r.q = std::max(r.q, 0.001);
  return r;
}

// H(s) = (n[0] s^2 + n[1] s + n[2]) / (d[0] s^2 + d[1] s + d[2])
struct Prototype {
  double n[3];
  double d[3];
};

Prototype prototype(Shape shape, const Reference& ref) {
  const double q = ref.q;
  const double A = ref.A;
  const double r = std::sqrt(A) / q;
  switch (shape) {
    case Shape::Lowpass:
      return {{0, 0, 1}, {1, 1 / q, 1}};
    case Shape::Highpass:
      return {{1, 0, 0}, {1, 1 / q, 1}};
    case Shape::Bandpass:
      return {{0, 1 / q, 0}, {1, 1 / q, 1}};
    case Shape::Notch:
      return {{1, 0, 1}, {1, 1 / q, 1}};
    case Shape::Peaking:
      return {{1, A / q, 1}, {1, 1 / (A * q), 1}};
    case Shape::LowShelf:
      return {{A, A * r, A * A}, {A, r, 1}};
    case Shape::HighShelf:
      return {{A * A, A * r, A}, {1, r, A}};
  }
  return {};
}

// |H| of the filter at the angular frequency w: z = e^jw gives
// s = j tan(w / 2) / K.
double cookbookMagnitude(const Design& d, const Reference& ref, double w) {
  const Prototype p = prototype(d.shape, ref);
  const std::complex<double> s(0.0, std::tan(w / 2.0) / std::tan(ref.w0 / 2.0));
  const std::complex<double> h =
      (p.n[0] * s * s + p.n[1] * s + p.n[2]) / (p.d[0] * s * s + p.d[1] * s + p.d[2]);
  return std::pow(std::abs(h), d.sections);
}

// b0, b1, b2, a1, a2 of one section, normalized by a0: the bilinear
// transform multiplied through by K^2 (1 + z^-1)^2.
std::array<double, 5> cookbookCoefs(const Design& d, const Reference& ref) {
  const Prototype p = prototype(d.shape, ref);
  const double K = std::tan(ref.w0 / 2.0);
  const auto z = [K](const double(&c)[3]) {
    return std::array<double, 3>{
        c[0] + c[1] * K + c[2] * K * K,
        2.0 * (c[2] * K * K - c[0]),
        c[0] - c[1] * K + c[2] * K * K,
    };
  };
  const std::array<double, 3> b = z(p.n);
  const std::array<double, 3> a = z(p.d);
  return {b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0]};
}

// ............................................................................
// Magnitudes

// A sine of amplitude 1, cached per frequency.
const std::vector<float>& sine(double w, int frames) {
  static std::map<double, std::vector<float>> cache;
  std::vector<float>& x = cache[w];
  if (x.empty()) {
    x.resize(frames);
    for (int i = 0; i < frames; ++i) x[i] = static_cast<float>(std::sin(w * i));
  }
  return x;
}

// The amplitude of the angular frequency w in x, over a whole number of
// periods.
double amplitude(const float* x, int frames, double w) {
  std::complex<double> sum = 0.0;
  std::complex<double> phasor = 1.0;
  const std::complex<double> rotation = std::polar(1.0, -w);
  for (int i = 0; i < frames; ++i) {
    sum += static_cast<double>(x[i]) * phasor;
    phasor *= rotation;
  }
  return 2.0 * std::abs(sum) / frames;
}

// Measures the gain of F for sines after settling, in 1 or 2 channels (the
// second with another amplitude and the opposite sign), and compares it
// with the cookbook.
template <class F>
void checkMagnitudes(const Design& d, int channels) {
  constexpr int kSampleRate = 48000;
  constexpr int kSettle = kSampleRate / 2;
  constexpr int kWindow = kSampleRate / 5;  // whole periods of multiples of 5 Hz
  constexpr int kSignalFrames = kSettle + kWindow;
  constexpr int kBlock = 512;
  const double probes[] = {20,   50,   100,  200,   315,   500,   800,   1250,
                           2000, 3150, 5000, 8000, 10000, 12500, 16000, 20000};
  const Settings settings[] = {{250, 0, 12, 1}, {2000, 9, -18, 0.5f}, {10000, -3, 6, 2}};
  const float amplitudes[2] = {0.5f, -0.25f};

  std::vector<float> buffers[2] = {std::vector<float>(kSignalFrames),
                                   std::vector<float>(kSignalFrames)};
  for (bool smoothing : {false, true}) {
    for (const Settings& s : settings) {
      std::vector<double> frequencies(std::begin(probes), std::end(probes));
      frequencies.push_back(s.cutoff);
      for (double frequency : frequencies) {
        const double w = 2.0 * kPi * frequency / kSampleRate;
        const std::vector<float>& x = sine(w, kSignalFrames);
        for (int ch = 0; ch < channels; ++ch) {
          for (int i = 0; i < kSignalFrames; ++i) buffers[ch][i] = amplitudes[ch] * x[i];
        }
        F f;
        f.init(kSampleRate);
        f.setSmoothingEnabled(smoothing);
        configure(f, s);
        for (int i = 0; i < kSignalFrames; i += kBlock) {
          float* io[2] = {buffers[0].data() + i, buffers[1].data() + i};
          f.compute(std::min(kBlock, kSignalFrames - i), io, io);
        }
        const double expected = cookbookMagnitude(d, reference(d, s, kSampleRate), w);
        for (int ch = 0; ch < channels; ++ch) {
          const double measured =
              amplitude(buffers[ch].data() + kSettle, kWindow, w) / std::abs(amplitudes[ch]);
          AUD_CHECK_NEAR(measured, expected, kMagnitudeTolerance * expected + kMagnitudeFloor);
        }
      }
    }
  }
}

// ............................................................................
// Impulses

enum class Smoothing { Off, On, AfterPrepare };

// The first samples of the response of `sections` sections to an impulse at
// frame k after silence, from the difference equation of each section
//
//     y[n] = b0[n] x[n] + b1[n-1] x[n-1] + b2[n-2] x[n-2]
//            - a1[n] y[n-1] - a2[n-1] y[n-2],
//
// the timing of Faust's fi.tf22t that the replaced filters had. While
// smoothing is on, each coefficient climbs from 0 as c[n] = c (1 - p^(n + 1))
// with p = exp(-1 / (1 ms Fs)); it is c when smoothing is off and after
// prepare(), which starts on the targets.
template <int Length>
std::array<double, Length> impulseResponse(const std::array<double, 5>& target, int sections,
                                           int k, Smoothing smoothing, double pole) {
  // coefficient i of b0, b1, b2, a1, a2 at frame n
  const auto c = [&](int i, int n) {
    return smoothing == Smoothing::On ? target[i] * (1.0 - std::pow(pole, n + 1)) : target[i];
  };
  std::array<double, Length> x{};
  x[0] = 1.0;
  for (int section = 0; section < sections; ++section) {
    std::array<double, Length> y{};
    for (int m = 0; m < Length; ++m) {
      const int n = k + m;
      y[m] = c(0, n) * x[m];
      if (m >= 1) y[m] += c(1, n - 1) * x[m - 1] - c(3, n) * y[m - 1];
      if (m >= 2) y[m] += c(2, n - 2) * x[m - 2] - c(4, n - 1) * y[m - 2];
    }
    x = y;
  }
  return x;
}

// Compares the response of F to an impulse at frame k after silence with
// impulseResponse(), in each channel.
template <class F>
void checkImpulse(const Design& d, int channels, const Settings& s, int k, Smoothing smoothing) {
  constexpr int kSampleRate = 44100;
  constexpr int kLength = 3;
  const float amplitudes[2] = {1.0f, -0.5f};
  const double pole = std::exp(-1.0 / (1e-3 * kSampleRate));
  const std::array<double, 5> target = cookbookCoefs(d, reference(d, s, kSampleRate));
  const std::array<double, kLength> expected =
      impulseResponse<kLength>(target, d.sections, k, smoothing, pole);
  double scale = 0.0;
  for (double e : expected) scale = std::max(scale, std::abs(e));

  F f;
  f.init(kSampleRate);
  f.setSmoothingEnabled(smoothing != Smoothing::Off);
  configure(f, s);
  if (smoothing == Smoothing::AfterPrepare) prepare(f, s);
  std::vector<float> buffers[2] = {std::vector<float>(k + kLength),
                                   std::vector<float>(k + kLength)};
  for (int ch = 0; ch < channels; ++ch) buffers[ch][k] = amplitudes[ch];
  float* io[2] = {buffers[0].data(), buffers[1].data()};
  f.compute(k + kLength, io, io);

  for (int ch = 0; ch < channels; ++ch) {
    for (int m = 0; m < kLength; ++m) {
      const double measured = buffers[ch][k + m] / amplitudes[ch];
      AUD_CHECK_NEAR(measured, expected[m], kImpulseTolerance * scale);
    }
  }
}

// Checks the coefficients, their smoothing and the timing of the sections
// with impulses after silence. Parameters beyond their limits and NaN check
// the conventions of sfz_filters.dsp; a few samples need not settle,
// whatever the Q.
template <class F>
void checkImpulses(const Design& d, int channels) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const Settings settings[] = {
      {3000, 6, 9, 0.7f},        // within the limits
      {1000, 70, 70, 50},        // resonance, gain and bandwidth beyond the upper limits
      {30000, 80, 80, 50},       // the cutoff too: the Q of the bandwidth is below 0.001
      {-50, -80, -200, 0.001f},  // everything beyond the lower limits
      {nan, nan, nan, nan},      // NaN counts as the lower limit
  };
  for (const Settings& s : settings) {
    for (int k : {0, 1, 10, 44, 300}) {
      for (Smoothing smoothing : {Smoothing::Off, Smoothing::On, Smoothing::AfterPrepare}) {
        checkImpulse<F>(d, channels, s, k, smoothing);
      }
    }
  }
}

// ............................................................................
// Sample rates below 40.8 kHz

// White noise in [-0.5, 0.5).
std::vector<float> noise(uint32_t seed) {
  std::vector<float> x(kFrames);
  for (float& v : x) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    v = static_cast<float>(seed / 4294967296.0 - 0.5);
  }
  return x;
}

// Noise through F at `sampleRate` with `cutoff`, a resonance of 12 dB, a
// gain of 6 dB and a bandwidth of an octave, in 1 or 2 channels.
template <class F>
std::vector<float> filteredNoise(int channels, int sampleRate, float cutoff) {
  std::vector<float> buffers[2] = {noise(0x2545F491u), noise(0x9E3779B9u)};
  F f;
  f.init(sampleRate);
  configure(f, {cutoff, 12, 6, 1});
  float* io[2] = {buffers[0].data(), buffers[1].data()};
  f.compute(kFrames, io, io);
  std::vector<float> output = buffers[0];
  if (channels == 2) output.insert(output.end(), buffers[1].begin(), buffers[1].end());
  return output;
}

// A cutoff at or above Nyquist, e.g. 20 kHz at 24 kHz, where the formulas
// turn unstable, acts as the highest cutoff below it: the output stays
// finite and equals that cutoff's.
template <class F>
void checkCutoffAboveNyquist(int channels) {
  for (int sampleRate : {16000, 24000, 32000}) {
    const float limit = static_cast<float>(sfz::rbj::kMaxCutoffPerSampleRate * sampleRate);
    const std::vector<float> above = filteredNoise<F>(channels, sampleRate, 20000.0f);
    AUD_CHECK(std::all_of(above.begin(), above.end(), [](float v) { return std::isfinite(v); }));
    AUD_CHECK(above == filteredNoise<F>(channels, sampleRate, limit));
  }
}

// ............................................................................
// Originals

#if HAVE_ORIGINALS

struct Change {
  int frame;
  Settings settings;
};

struct Scenario {
  int sampleRate;
  std::vector<Change> changes;  // the first one at frame 0
};

// The largest difference of the output from the original's.
constexpr double kOriginalTolerance = 1e-5;
// The largest output of the original the inputs are scaled down to, so that
// the absolute difference stays meaningful for float output.
constexpr double kOriginalPeak = 4.0;

std::vector<Scenario> scenarios() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  std::vector<Scenario> list;
  // steady
  list.push_back({48000, {{0, {1000, 3, 6, 1}}}});
  // jumps
  list.push_back({44100,
                  {
                      {0, {800, 6, 9, 1}},
                      {3000, {5000, -6, -12, 0.3f}},
                      {7001, {120, 12, 18, 2.5f}},
                      {12000, {15000, 0, -3, 4}},
                      {16003, {440, 3, 0, 1}},
                  }});
  // the cutoff, resonance, gain and bandwidth of an LFO every 16 frames,
  // like sfz::Filter::processModulated()
  Scenario modulated{48000, {}};
  for (int frame = 0; frame < kFrames; frame += 16) {
    const double t = frame / 48000.0;
    modulated.changes.push_back(
        {frame,
         {
             static_cast<float>(1000.0 * std::exp2(2.5 * std::sin(2.0 * kPi * 3.0 * t))),
             static_cast<float>(6.0 + 6.0 * std::sin(2.0 * kPi * 5.0 * t)),
             static_cast<float>(12.0 * std::sin(2.0 * kPi * 2.0 * t)),
             static_cast<float>(1.5 + std::sin(2.0 * kPi * 4.0 * t)),
         }});
  }
  list.push_back(modulated);
  // every parameter beyond its limits, and NaN
  list.push_back({96000,
                  {
                      {0, {-50, -80, -200, 0.001f}},
                      {4000, {30000, 80, 80, 50}},
                      {8000, {nan, nan, nan, nan}},
                      {12000, {0, 61, 61, 13}},
                      {16000, {25000, -61, -121, 0.005f}},
                  }});
  return list;
}

using Signal = std::vector<std::vector<float>>;  // channels of kFrames

// An exponential sweep from 20 Hz to 20 kHz with an amplitude of 0.5.
std::vector<float> sweep(int sampleRate) {
  std::vector<float> x(kFrames);
  const double duration = static_cast<double>(kFrames) / sampleRate;
  const double k = std::log(1000.0) / duration;
  for (int i = 0; i < kFrames; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    x[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 20.0 * (std::exp(k * t) - 1.0) / k));
  }
  return x;
}

const int kBlocks[] = {64, 0, 1, 17, 256, 5, 128, 33, 512, 2};
constexpr int kNumBlocks = sizeof(kBlocks) / sizeof(kBlocks[0]);

// Runs `dsp` over `input` scaled by `gain`, in blocks of varying sizes, and
// applies the changes of the scenario at their frames. In place, the input
// is copied to the output and filtered there, like sfizz voices do.
template <class Dsp>
Signal render(Dsp& dsp, const Scenario& sc, const Signal& input, double gain, bool smoothing,
              bool prepared, bool inPlace) {
  const int channels = static_cast<int>(input.size());
  Signal scaled = input;
  for (auto& channel : scaled) {
    for (float& v : channel) v = static_cast<float>(v * gain);
  }
  Signal output(channels, std::vector<float>(kFrames));

  dsp.init(sc.sampleRate);
  dsp.setSmoothingEnabled(smoothing);
  if (prepared) prepare(dsp, sc.changes.front().settings);

  size_t next = 0;
  int frame = 0;
  for (int b = 0; frame < kFrames; ++b) {
    while (next < sc.changes.size() && sc.changes[next].frame <= frame) {
      configure(dsp, sc.changes[next++].settings);
    }
    int count = std::min(kBlocks[b % kNumBlocks], kFrames - frame);
    if (next < sc.changes.size()) count = std::min(count, sc.changes[next].frame - frame);

    const float* in[2] = {};
    float* out[2] = {};
    for (int ch = 0; ch < channels; ++ch) {
      out[ch] = output[ch].data() + frame;
      in[ch] = inPlace ? out[ch] : scaled[ch].data() + frame;
      if (inPlace) {
        std::copy(scaled[ch].begin() + frame, scaled[ch].begin() + frame + count, out[ch]);
      }
    }
    dsp.compute(count, in, out);
    frame += count;
  }
  return output;
}

double peak(const Signal& s) {
  double p = 0.0;
  for (const auto& channel : s) {
    for (float v : channel) p = std::max(p, static_cast<double>(std::abs(v)));
  }
  return p;
}

// The largest |new - original| over a run; infinite where only one of them
// is NaN.
double difference(const Signal& expected, const Signal& actual) {
  double largest = 0.0;
  for (size_t ch = 0; ch < expected.size(); ++ch) {
    for (int i = 0; i < kFrames; ++i) {
      const double e = expected[ch][i];
      const double a = actual[ch][i];
      largest = std::isnan(e) == std::isnan(a) ? std::max(largest, std::abs(a - e))
                                               : std::numeric_limits<double>::infinity();
    }
  }
  return largest;
}

// Renders the scenario through the new and the original filter and checks
// their difference. The input is scaled down when the original's output
// would exceed kOriginalPeak; the filters are linear in their input.
template <class New, class Old>
void compareRun(const Scenario& sc, const Signal& input, bool smoothing, bool prepared,
                bool inPlace) {
  Old original;
  Signal expected = render(original, sc, input, 1.0, smoothing, prepared, inPlace);
  double gain = 1.0;
  if (peak(expected) > kOriginalPeak) {
    gain = kOriginalPeak / peak(expected);
    expected = render(original, sc, input, gain, smoothing, prepared, inPlace);
  }
  New filter;
  const Signal actual = render(filter, sc, input, gain, smoothing, prepared, inPlace);
  AUD_CHECK_NEAR(difference(expected, actual), 0.0, kOriginalTolerance);
}

// Every scenario with noise, a sweep and both in two channels, with and
// without smoothing and prepare().
template <class New, class New2ch, class Old, class Old2ch>
void compareWithOriginal() {
  for (const Scenario& sc : scenarios()) {
    const Signal noiseInput{noise(0x2545F491u)};
    const Signal sweepInput{sweep(sc.sampleRate)};
    const Signal stereoInput{noise(0x9E3779B9u), sweep(sc.sampleRate)};
    for (bool smoothing : {false, true}) {
      for (bool prepared : {false, true}) {
        compareRun<New, Old>(sc, noiseInput, smoothing, prepared, false);
        compareRun<New, Old>(sc, sweepInput, smoothing, prepared, true);
        compareRun<New2ch, Old2ch>(sc, stereoInput, smoothing, prepared, true);
      }
    }
  }
}

#endif

}  // namespace

// ############################################################################
// The tests of each filter

#define RBJ_TESTS(name, shape, sections, kind)              \
  AUD_TEST(rbj_##name##_keeps_its_parameters) {              \
    checkParameters<sfz##name, Kind::kind>();                \
    checkParameters<sfz2ch##name, Kind::kind>();             \
  }                                                          \
  AUD_TEST(rbj_##name##_follows_the_cookbook_magnitudes) {   \
    const Design design{Shape::shape, sections, Kind::kind}; \
    checkMagnitudes<sfz##name>(design, 1);                   \
    checkMagnitudes<sfz2ch##name>(design, 2);                \
  }                                                          \
  AUD_TEST(rbj_##name##_follows_the_cookbook_impulses) {     \
    const Design design{Shape::shape, sections, Kind::kind}; \
    checkImpulses<sfz##name>(design, 1);                     \
    checkImpulses<sfz2ch##name>(design, 2);                  \
  }                                                          \
  AUD_TEST(rbj_##name##_stays_finite_above_nyquist) {        \
    checkCutoffAboveNyquist<sfz##name>(1);                   \
    checkCutoffAboveNyquist<sfz2ch##name>(2);                \
  }
RBJ_FILTERS(RBJ_TESTS)
#undef RBJ_TESTS

#if HAVE_ORIGINALS
#define RBJ_ORIGINAL_TESTS(name, shape, sections, kind)            \
  AUD_TEST(rbj_##name##_matches_the_faust_original) {              \
    compareWithOriginal<sfz##name, sfz2ch##name, original::sfz##name, \
                        original::sfz2ch##name>();                 \
  }
RBJ_FILTERS(RBJ_ORIGINAL_TESTS)
#undef RBJ_ORIGINAL_TESTS
#endif

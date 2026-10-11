// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Tests the RBJ biquads that replace the Faust-generated resonant, peaking
// and shelving filters of the sfizz fork (src/sfizz/RbjFilters.hpp):
//
// - parameters: the setters and getters of each filter and their defaults
// - magnitude: the steady-state response to sines, with coefficient
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
//
// The tests run with the other native tests of the package:
//
//   AUD_SFIZZ_PRISTINE=<sfizz checkout> node scripts/test-native.js

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <map>
#include <math.h>
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

/// The parameters of a filter: cutoff and resonance, plus gain for peaks and
/// shelves; cutoff, gain and bandwidth for equalizer bands
enum class Kind { Resonant, PeakShelf, Eq };

// name, shape, sections, kind
#define RBJ_FILTERS(F)                 \
    F(Lpf2p, Lowpass, 1, Resonant)     \
    F(Lpf4p, Lowpass, 2, Resonant)     \
    F(Lpf6p, Lowpass, 3, Resonant)     \
    F(Hpf2p, Highpass, 1, Resonant)    \
    F(Hpf4p, Highpass, 2, Resonant)    \
    F(Hpf6p, Highpass, 3, Resonant)    \
    F(Bpf2p, Bandpass, 1, Resonant)    \
    F(Bpf4p, Bandpass, 2, Resonant)    \
    F(Bpf6p, Bandpass, 3, Resonant)    \
    F(Brf2p, Notch, 1, Resonant)       \
    F(Lsh, LowShelf, 1, PeakShelf)     \
    F(Hsh, HighShelf, 1, PeakShelf)    \
    F(Peq, Peaking, 1, PeakShelf)      \
    F(EqPeak, Peaking, 1, Eq)          \
    F(EqLshelf, LowShelf, 1, Eq)       \
    F(EqHshelf, HighShelf, 1, Eq)

struct Design {
    Shape shape;
    int sections;
    Kind kind;
};

struct Settings {
    float cutoff; // Hz
    float resonance; // dB
    float gain; // dB, of peaks and shelves
    float bandwidth; // octaves, of equalizer bands
};

constexpr Settings kDefaults { 440.0f, 0.0f, 0.0f, 1.0f };
constexpr double kPi = 3.141592653589793238462643383280;

bool operator==(const Settings& a, const Settings& b)
{
    return a.cutoff == b.cutoff && a.resonance == b.resonance && a.gain == b.gain && a.bandwidth == b.bandwidth;
}

int expect(bool ok, const char* filter, const char* what)
{
    if (!ok)
        std::printf("FAIL %s: %s\n", filter, what);
    return ok ? 0 : 1;
}

/// Sets the parameters through the wrappers of SfzFilterImpls.hpp. Each
/// filter takes the call that matches its kind; the other one does nothing.
template <class Dsp>
void configure(Dsp& dsp, const Settings& s)
{
    dsp.configureStandard(s.cutoff, s.resonance, s.gain);
    dsp.configureEq(s.cutoff, s.bandwidth, s.gain);
}

/// What sfz::Filter::prepare() does: start on the settings without smoothing
template <class Dsp>
void prepare(Dsp& dsp, const Settings& s)
{
    float buffer[2] = {};
    float* inout[2] = { &buffer[0], &buffer[1] };
    const bool smoothing = dsp.isSmoothingEnabled();
    dsp.instanceClear();
    configure(dsp, s);
    dsp.setSmoothingEnabled(false);
    dsp.compute(1, inout, inout);
    dsp.setSmoothingEnabled(smoothing);
}

//------------------------------------------------------------------------------
// Parameters

/// The parameters of a filter of kind K; those it lacks read as the defaults
template <Kind K, class F>
Settings parametersOf(const F& f)
{
    Settings s = kDefaults;
    s.cutoff = f.getCutoff();
    if constexpr (K != Kind::Eq)
        s.resonance = f.getResonance();
    if constexpr (K != Kind::Resonant)
        s.gain = f.getPeakShelfGain();
    if constexpr (K == Kind::Eq)
        s.bandwidth = f.getBandwidth();
    return s;
}

/// What a filter of kind K keeps of s
template <Kind K>
Settings keptOf(Settings s)
{
    if (K == Kind::Eq)
        s.resonance = kDefaults.resonance;
    if (K == Kind::Resonant)
        s.gain = kDefaults.gain;
    if (K != Kind::Eq)
        s.bandwidth = kDefaults.bandwidth;
    return s;
}

template <class F, Kind K>
int checkParameters(const char* name)
{
    const Settings set { 1234.5f, -7.25f, 3.5f, 0.75f };
    F f;
    f.init(48000);
    int failed = expect(parametersOf<K>(f) == kDefaults, name, "defaults after init");
    configure(f, set);
    failed += expect(parametersOf<K>(f) == keptOf<K>(set), name, "setters and getters");
    f.init(44100);
    failed += expect(parametersOf<K>(f) == kDefaults, name, "init restores the defaults");
    return failed;
}

int checkAllParameters()
{
    int failed = 0;
#define CHECK(name, shape, sections, kind)                              \
    failed += checkParameters<sfz##name, Kind::kind>(#name);            \
    failed += checkParameters<sfz2ch##name, Kind::kind>("2ch" #name);
    RBJ_FILTERS(CHECK)
#undef CHECK
    std::printf("parameters: %s\n", failed ? "FAILED" : "ok");
    return failed;
}

//------------------------------------------------------------------------------
// The reference: the cookbook's analog prototypes, which its bilinear
// transform s = (1 - z^-1) / ((1 + z^-1) K), K = tan(w0 / 2), turns into
// the biquads, with the parameter conventions of sfz_filters.dsp

/// w0, Q and A of one section
struct Reference {
    double w0;
    double q;
    double A;
};

/// x within [lo, hi]; NaN counts as lo
double limited(float x, double lo, double hi)
{
    return std::isnan(x) ? lo : std::min(std::max(static_cast<double>(x), lo), hi);
}

/// The settings as sfz_filters.dsp takes them: each parameter within its
/// limits, the bandwidth as the cookbook's BW case, Q at least 0.001
Reference reference(const Design& d, const Settings& s, int sampleRate)
{
    Reference r;
    r.w0 = 2.0 * kPi * limited(s.cutoff, 1.0, 20000.0) / sampleRate;
    r.A = d.kind == Kind::Resonant ? 1.0 : std::pow(10.0, limited(s.gain, -120.0, 60.0) / 40.0);
    if (d.kind == Kind::Eq) {
        const double bw = limited(s.bandwidth, 0.01, 12.0);
        const double alpha = std::sin(r.w0) * std::sinh(std::log(2.0) / 2.0 * bw * r.w0 / std::sin(r.w0));
        r.q = std::sin(r.w0) / (2.0 * alpha);
    } else {
        r.q = std::pow(10.0, limited(s.resonance, -60.0, 60.0) / 20.0);
    }
    r.q = std::max(r.q, 0.001);
    return r;
}

/// H(s) = (n[0] s^2 + n[1] s + n[2]) / (d[0] s^2 + d[1] s + d[2])
struct Prototype {
    double n[3];
    double d[3];
};

Prototype prototype(Shape shape, const Reference& ref)
{
    const double q = ref.q;
    const double A = ref.A;
    const double r = std::sqrt(A) / q;
    switch (shape) {
    case Shape::Lowpass: return { { 0, 0, 1 }, { 1, 1 / q, 1 } };
    case Shape::Highpass: return { { 1, 0, 0 }, { 1, 1 / q, 1 } };
    case Shape::Bandpass: return { { 0, 1 / q, 0 }, { 1, 1 / q, 1 } };
    case Shape::Notch: return { { 1, 0, 1 }, { 1, 1 / q, 1 } };
    case Shape::Peaking: return { { 1, A / q, 1 }, { 1, 1 / (A * q), 1 } };
    case Shape::LowShelf: return { { A, A * r, A * A }, { A, r, 1 } };
    case Shape::HighShelf: return { { A * A, A * r, A }, { 1, r, A } };
    }
    return {};
}

/// |H| of the filter at the angular frequency w: z = e^jw gives
/// s = j tan(w / 2) / K
double cookbookMagnitude(const Design& d, const Reference& ref, double w)
{
    const Prototype p = prototype(d.shape, ref);
    const std::complex<double> s(0.0, std::tan(w / 2.0) / std::tan(ref.w0 / 2.0));
    const std::complex<double> h = (p.n[0] * s * s + p.n[1] * s + p.n[2]) / (p.d[0] * s * s + p.d[1] * s + p.d[2]);
    return std::pow(std::abs(h), d.sections);
}

/// b0, b1, b2, a1, a2 of one section, normalized by a0: the bilinear
/// transform multiplied through by K^2 (1 + z^-1)^2
std::array<double, 5> cookbookCoefs(const Design& d, const Reference& ref)
{
    const Prototype p = prototype(d.shape, ref);
    const double K = std::tan(ref.w0 / 2.0);
    const auto z = [K](const double (&c)[3]) {
        return std::array<double, 3> {
            c[0] + c[1] * K + c[2] * K * K,
            2.0 * (c[2] * K * K - c[0]),
            c[0] - c[1] * K + c[2] * K * K,
        };
    };
    const std::array<double, 3> b = z(p.n);
    const std::array<double, 3> a = z(p.d);
    return { b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0] };
}

//------------------------------------------------------------------------------
// Magnitude

/// A sine of amplitude 1, cached per frequency
const std::vector<float>& sine(double w, int frames)
{
    static std::map<double, std::vector<float>> cache;
    std::vector<float>& x = cache[w];
    if (x.empty()) {
        x.resize(frames);
        for (int i = 0; i < frames; ++i)
            x[i] = static_cast<float>(std::sin(w * i));
    }
    return x;
}

/// Amplitude of the angular frequency w in x, over a whole number of periods
double amplitude(const float* x, int frames, double w)
{
    std::complex<double> sum = 0.0;
    std::complex<double> phasor = 1.0;
    const std::complex<double> rotation = std::polar(1.0, -w);
    for (int i = 0; i < frames; ++i) {
        sum += static_cast<double>(x[i]) * phasor;
        phasor *= rotation;
    }
    return 2.0 * std::abs(sum) / frames;
}

/**
   Measures the gain of `f` for sines after settling, in 1 or 2 channels
   (the second with another amplitude and the opposite sign), and compares
   it with the cookbook. Returns the number of failures and raises
   `worst` to the largest relative deviation.
 */
template <class F>
int checkMagnitude(const char* name, const Design& d, int channels, double& worst)
{
    constexpr int sampleRate = 48000;
    constexpr int settle = sampleRate / 2;
    constexpr int window = sampleRate / 5; // whole periods of multiples of 5 Hz
    constexpr int frames = settle + window;
    constexpr int block = 512;
    const double probes[] = { 20, 50, 100, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 10000, 12500, 16000, 20000 };
    const Settings settings[] = { { 250, 0, 12, 1 }, { 2000, 9, -18, 0.5f }, { 10000, -3, 6, 2 } };
    const float amplitudes[2] = { 0.5f, -0.25f };

    int failed = 0;
    std::vector<float> buffers[2] = { std::vector<float>(frames), std::vector<float>(frames) };
    for (bool smoothing : { false, true }) {
        for (const Settings& s : settings) {
            std::vector<double> frequencies(std::begin(probes), std::end(probes));
            frequencies.push_back(s.cutoff);
            for (double frequency : frequencies) {
                const double w = 2.0 * kPi * frequency / sampleRate;
                const std::vector<float>& x = sine(w, frames);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < frames; ++i)
                        buffers[ch][i] = amplitudes[ch] * x[i];

                F f;
                f.init(sampleRate);
                f.setSmoothingEnabled(smoothing);
                configure(f, s);
                for (int i = 0; i < frames; i += block) {
                    float* io[2] = { buffers[0].data() + i, buffers[1].data() + i };
                    f.compute(std::min(block, frames - i), io, io);
                }

                const double expected = cookbookMagnitude(d, reference(d, s, sampleRate), w);
                for (int ch = 0; ch < channels; ++ch) {
                    const double measured = amplitude(buffers[ch].data() + settle, window, w) / std::abs(amplitudes[ch]);
                    const double deviation = std::abs(measured - expected);
                    worst = std::max(worst, deviation / std::max(expected, 1e-2));
                    if (deviation > 1e-5 * expected + 1e-7) {
                        std::printf("FAIL %s: %g Hz with cutoff %g Hz, smoothing %s, channel %d: |H| = %.9g, cookbook %.9g\n",
                                    name, frequency, s.cutoff, smoothing ? "on" : "off", ch, measured, expected);
                        ++failed;
                    }
                }
            }
        }
    }
    return failed;
}

int checkAllMagnitudes()
{
    int failed = 0;
#define CHECK(name, shape, sections, kind)                                       \
    {                                                                            \
        const Design d { Shape::shape, sections, Kind::kind };                   \
        double worst = 0.0;                                                      \
        failed += checkMagnitude<sfz##name>(#name, d, 1, worst);                 \
        failed += checkMagnitude<sfz2ch##name>("2ch" #name, d, 2, worst);        \
        std::printf("magnitude %-9s largest relative deviation from the "        \
                    "cookbook %.2e\n", #name, worst);                            \
    }
    RBJ_FILTERS(CHECK)
#undef CHECK
    std::printf("magnitude: %s\n", failed ? "FAILED" : "ok");
    return failed;
}

//------------------------------------------------------------------------------
// Impulses

enum class Smoothing { Off, On, AfterPrepare };

/**
   The first samples of the response of `sections` sections to an impulse at
   frame k after silence, from the difference equation of each section

       y[n] = b0[n] x[n] + b1[n-1] x[n-1] + b2[n-2] x[n-2]
              - a1[n] y[n-1] - a2[n-1] y[n-2],

   the timing of Faust's fi.tf22t that the replaced filters had. While
   smoothing is on, each coefficient climbs from 0 as c[n] = c (1 - p^(n + 1))
   with p = exp(-1 / (1 ms Fs)); it is c when smoothing is off and after
   prepare(), which starts on the targets.
 */
template <int Length>
std::array<double, Length> impulseResponse(const std::array<double, 5>& target, int sections, int k, Smoothing smoothing, double pole)
{
    // coefficient i of b0, b1, b2, a1, a2 at frame n
    const auto c = [&](int i, int n) {
        return smoothing == Smoothing::On ? target[i] * (1.0 - std::pow(pole, n + 1)) : target[i];
    };
    std::array<double, Length> x {};
    x[0] = 1.0;
    for (int section = 0; section < sections; ++section) {
        std::array<double, Length> y {};
        for (int m = 0; m < Length; ++m) {
            const int n = k + m;
            y[m] = c(0, n) * x[m];
            if (m >= 1)
                y[m] += c(1, n - 1) * x[m - 1] - c(3, n) * y[m - 1];
            if (m >= 2)
                y[m] += c(2, n - 2) * x[m - 2] - c(4, n - 1) * y[m - 2];
        }
        x = y;
    }
    return x;
}

/// Compares the response of `F` to an impulse at frame k after silence with
/// impulseResponse(), in each channel
template <class F>
int checkImpulse(const char* name, const Design& d, int channels, const Settings& s, int k, Smoothing smoothing)
{
    constexpr int sampleRate = 44100;
    constexpr int length = 3;
    const char* modes[] = { "off", "on", "on after prepare()" };
    const float amplitudes[2] = { 1.0f, -0.5f };
    const double pole = std::exp(-1.0 / (1e-3 * sampleRate));
    const std::array<double, 5> target = cookbookCoefs(d, reference(d, s, sampleRate));
    const std::array<double, length> expected = impulseResponse<length>(target, d.sections, k, smoothing, pole);
    double scale = 0.0;
    for (double e : expected)
        scale = std::max(scale, std::abs(e));

    F f;
    f.init(sampleRate);
    f.setSmoothingEnabled(smoothing != Smoothing::Off);
    configure(f, s);
    if (smoothing == Smoothing::AfterPrepare)
        prepare(f, s);
    std::vector<float> buffers[2] = { std::vector<float>(k + length), std::vector<float>(k + length) };
    for (int ch = 0; ch < channels; ++ch)
        buffers[ch][k] = amplitudes[ch];
    float* io[2] = { buffers[0].data(), buffers[1].data() };
    f.compute(k + length, io, io);

    int failed = 0;
    for (int ch = 0; ch < channels; ++ch)
        for (int m = 0; m < length; ++m) {
            const double measured = buffers[ch][k + m] / amplitudes[ch];
            if (!(std::abs(measured - expected[m]) <= 1e-6 * scale)) {
                std::printf("FAIL %s: cutoff %g Hz, impulse at frame %d, smoothing %s, channel %d, sample %d: %.9g, expected %.9g\n",
                            name, s.cutoff, k, modes[static_cast<int>(smoothing)], ch, m, measured, expected[m]);
                ++failed;
            }
        }
    return failed;
}

/**
   Checks the coefficients, their smoothing and the timing of the sections
   with impulses after silence. Parameters beyond their limits and NaN check
   the conventions of sfz_filters.dsp; a few samples need not settle,
   whatever the Q.
 */
template <class F>
int checkImpulses(const char* name, const Design& d, int channels)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Settings settings[] = {
        { 3000, 6, 9, 0.7f }, // within the limits
        { 1000, 70, 70, 50 }, // resonance, gain and bandwidth beyond the upper limits
        { 30000, 80, 80, 50 }, // the cutoff too: the Q of the bandwidth is below 0.001
        { -50, -80, -200, 0.001f }, // everything beyond the lower limits
        { nan, nan, nan, nan }, // NaN counts as the lower limit
    };
    int failed = 0;
    for (const Settings& s : settings)
        for (int k : { 0, 1, 10, 44, 300 })
            for (Smoothing smoothing : { Smoothing::Off, Smoothing::On, Smoothing::AfterPrepare })
                failed += checkImpulse<F>(name, d, channels, s, k, smoothing);
    return failed;
}

int checkAllImpulses()
{
    int failed = 0;
#define CHECK(name, shape, sections, kind)                              \
    {                                                                   \
        const Design d { Shape::shape, sections, Kind::kind };          \
        failed += checkImpulses<sfz##name>(#name, d, 1);                \
        failed += checkImpulses<sfz2ch##name>("2ch" #name, d, 2);       \
    }
    RBJ_FILTERS(CHECK)
#undef CHECK
    std::printf("impulses: %s\n", failed ? "FAILED" : "ok");
    return failed;
}

//------------------------------------------------------------------------------
// Originals

#if HAVE_ORIGINALS

struct Change {
    int frame;
    Settings settings;
};

struct Scenario {
    const char* name;
    int sampleRate;
    std::vector<Change> changes; // the first one at frame 0
};

constexpr int kFrames = 24000;

std::vector<Scenario> scenarios()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<Scenario> list;
    list.push_back({ "steady", 48000, { { 0, { 1000, 3, 6, 1 } } } });
    list.push_back({ "jumps", 44100, {
        { 0, { 800, 6, 9, 1 } },
        { 3000, { 5000, -6, -12, 0.3f } },
        { 7001, { 120, 12, 18, 2.5f } },
        { 12000, { 15000, 0, -3, 4 } },
        { 16003, { 440, 3, 0, 1 } },
    } });
    // the cutoff, resonance, gain and bandwidth of an LFO every 16 frames,
    // like sfz::Filter::processModulated()
    Scenario modulated { "modulated", 48000, {} };
    for (int frame = 0; frame < kFrames; frame += 16) {
        const double t = frame / 48000.0;
        modulated.changes.push_back({ frame, {
            static_cast<float>(1000.0 * std::exp2(2.5 * std::sin(2.0 * kPi * 3.0 * t))),
            static_cast<float>(6.0 + 6.0 * std::sin(2.0 * kPi * 5.0 * t)),
            static_cast<float>(12.0 * std::sin(2.0 * kPi * 2.0 * t)),
            static_cast<float>(1.5 + std::sin(2.0 * kPi * 4.0 * t)),
        } });
    }
    list.push_back(modulated);
    // every parameter beyond its limits, and NaN
    list.push_back({ "limits", 96000, {
        { 0, { -50, -80, -200, 0.001f } },
        { 4000, { 30000, 80, 80, 50 } },
        { 8000, { nan, nan, nan, nan } },
        { 12000, { 0, 61, 61, 13 } },
        { 16000, { 25000, -61, -121, 0.005f } },
    } });
    return list;
}

using Signal = std::vector<std::vector<float>>; // channels of kFrames

/// White noise in [-0.5, 0.5)
std::vector<float> noise(uint32_t seed)
{
    std::vector<float> x(kFrames);
    for (float& v : x) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        v = static_cast<float>(seed / 4294967296.0 - 0.5);
    }
    return x;
}

/// Exponential sweep from 20 Hz to 20 kHz with an amplitude of 0.5
std::vector<float> sweep(int sampleRate)
{
    std::vector<float> x(kFrames);
    const double duration = static_cast<double>(kFrames) / sampleRate;
    const double k = std::log(1000.0) / duration;
    for (int i = 0; i < kFrames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        x[i] = static_cast<float>(0.5 * std::sin(2.0 * kPi * 20.0 * (std::exp(k * t) - 1.0) / k));
    }
    return x;
}

const int kBlocks[] = { 64, 0, 1, 17, 256, 5, 128, 33, 512, 2 };

/**
   Runs `dsp` over `input` scaled by `gain`, in blocks of varying sizes, and
   applies the changes of the scenario at their frames. In place, the input
   is copied to the output and filtered there, like sfizz voices do.
 */
template <class Dsp>
Signal render(Dsp& dsp, const Scenario& sc, const Signal& input, double gain, bool smoothing, bool prepared, bool inPlace)
{
    const int channels = static_cast<int>(input.size());
    Signal scaled = input;
    for (auto& channel : scaled)
        for (float& v : channel)
            v = static_cast<float>(v * gain);
    Signal output(channels, std::vector<float>(kFrames));

    dsp.init(sc.sampleRate);
    dsp.setSmoothingEnabled(smoothing);
    if (prepared)
        prepare(dsp, sc.changes.front().settings);

    size_t next = 0;
    int frame = 0;
    for (int b = 0; frame < kFrames; ++b) {
        while (next < sc.changes.size() && sc.changes[next].frame <= frame)
            configure(dsp, sc.changes[next++].settings);
        int count = std::min(kBlocks[b % (sizeof(kBlocks) / sizeof(kBlocks[0]))], kFrames - frame);
        if (next < sc.changes.size())
            count = std::min(count, sc.changes[next].frame - frame);

        const float* in[2] = {};
        float* out[2] = {};
        for (int ch = 0; ch < channels; ++ch) {
            out[ch] = output[ch].data() + frame;
            in[ch] = inPlace ? out[ch] : scaled[ch].data() + frame;
            if (inPlace)
                std::copy(scaled[ch].begin() + frame, scaled[ch].begin() + frame + count, out[ch]);
        }
        dsp.compute(count, in, out);
        frame += count;
    }
    return output;
}

double peak(const Signal& s)
{
    double p = 0.0;
    for (const auto& channel : s)
        for (float v : channel)
            p = std::max(p, static_cast<double>(std::abs(v)));
    return p;
}

struct Difference {
    double mono = 0.0; // largest |new - original| of 1-channel runs
    double stereo = 0.0; // the same for 2-channel runs
    double output = 0.0; // largest |original|
    double minGain = 1.0; // smallest input gain
    int runs = 0;
};

/**
   Renders the scenario through the new and the original filter and records
   the largest difference. The input is scaled down when the original's
   output would exceed 4, so that the absolute difference stays meaningful
   for float output; the filters are linear in their input.
 */
template <class New, class Old>
void compareRun(const Scenario& sc, const Signal& input, bool smoothing, bool prepared, bool inPlace, Difference& d)
{
    Old original;
    Signal expected = render(original, sc, input, 1.0, smoothing, prepared, inPlace);
    double gain = 1.0;
    if (peak(expected) > 4.0) {
        gain = 4.0 / peak(expected);
        expected = render(original, sc, input, gain, smoothing, prepared, inPlace);
    }
    New filter;
    const Signal actual = render(filter, sc, input, gain, smoothing, prepared, inPlace);

    double difference = 0.0;
    for (size_t ch = 0; ch < input.size(); ++ch)
        for (int i = 0; i < kFrames; ++i) {
            const double e = expected[ch][i];
            const double a = actual[ch][i];
            difference = std::isnan(e) == std::isnan(a) ? std::max(difference, std::abs(a - e)) : INFINITY;
        }
    double& slot = input.size() == 1 ? d.mono : d.stereo;
    slot = std::max(slot, difference);
    d.output = std::max(d.output, peak(expected));
    d.minGain = std::min(d.minGain, gain);
    ++d.runs;
}

template <class New, class New2ch, class Old, class Old2ch>
int compareWithOriginal(const char* name)
{
    Difference d;
    for (const Scenario& sc : scenarios()) {
        const Signal noiseInput { noise(0x2545F491u) };
        const Signal sweepInput { sweep(sc.sampleRate) };
        const Signal stereoInput { noise(0x9E3779B9u), sweep(sc.sampleRate) };
        for (bool smoothing : { false, true })
            for (bool prepared : { false, true }) {
                compareRun<New, Old>(sc, noiseInput, smoothing, prepared, false, d);
                compareRun<New, Old>(sc, sweepInput, smoothing, prepared, true, d);
                compareRun<New2ch, Old2ch>(sc, stereoInput, smoothing, prepared, true, d);
            }
    }
    const double largest = std::max(d.mono, d.stereo);
    std::printf("original %-9s largest |new - original| %.2e (1 channel %.2e, 2 channels %.2e), "
                "largest output %.2f, smallest input gain %.1e, %d runs\n",
                name, largest, d.mono, d.stereo, d.output, d.minGain, d.runs);
    return expect(largest < 1e-5, name, "differs from the original by 1e-5 or more");
}

int checkAllAgainstOriginals()
{
    int failed = 0;
#define CHECK(name, shape, sections, kind)                                               \
    failed += compareWithOriginal<sfz##name, sfz2ch##name,                               \
                                  original::sfz##name, original::sfz2ch##name>(#name);
    RBJ_FILTERS(CHECK)
#undef CHECK
    std::printf("originals: %s\n", failed ? "FAILED" : "ok");
    return failed;
}

#endif

} // namespace

AUD_TEST(rbj_filters_keep_their_parameters) { AUD_CHECK(checkAllParameters() == 0); }

AUD_TEST(rbj_filters_follow_the_cookbook_magnitudes) { AUD_CHECK(checkAllMagnitudes() == 0); }

AUD_TEST(rbj_filters_follow_the_cookbook_impulses) { AUD_CHECK(checkAllImpulses() == 0); }

#if HAVE_ORIGINALS
AUD_TEST(rbj_filters_match_the_faust_originals) { AUD_CHECK(checkAllAgainstOriginals() == 0); }
#endif

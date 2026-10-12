// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2026, Audanika
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The 2-pole lowpass, highpass, bandpass, notch, peaking and shelving filters
// of sfizz, the 4-pole and 6-pole cascades of the first three, for 1 and 2
// channels.
//
// These biquads implement the formulas of Robert Bristow-Johnson's Audio EQ
// Cookbook (W3C Note, https://www.w3.org/TR/audio-eq-cookbook/) with the
// parameter conventions of sfizz's BSD-2-Clause sfz_filters.dsp by Jean
// Pierre Cimalando. They replace Faust output that was generated from
// LGPL-with-exception library code (ticket 25 of aud_audio_pm, decision
// recorded there), under the class names of that output, so that
// SfzFilterImpls.hpp wraps them as before. Include this header after the
// declaration of sfzFilterDsp.
//
// The file is Audanika's own code under the MIT license of the
// aud_dsp_sampler package, the LICENSE file in the root of its repository,
// not under sfizz's BSD-2-Clause license; src/third_party/NOTICES.md lists
// it apart from sfizz.

#pragma once

#include <algorithm>
#include <cmath>

namespace sfz {
namespace rbj {

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kLn2 = 0.693147180559945309417232121458;

/// Time constant of the coefficient smoothing, in seconds
constexpr double kSmoothingTime = 1e-3;

/// The highest cutoff as a share of the sample rate: below Nyquist, where
/// the formulas hold. It limits only rates below 40.8 kHz, where the cutoff
/// limit of 20 kHz lies above it.
constexpr double kMaxCutoffPerSampleRate = 0.49;

/**
   Biquad coefficients normalized by a0:
   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
 */
struct Coefs {
    double b0, b1, b2, a1, a2;
};

inline Coefs normalize(double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

//------------------------------------------------------------------------------
// Parameters as sfz_filters.dsp takes them

/// x limited to [lo, hi]; NaN gives lo
inline double clamp(double x, double lo, double hi)
{
    return std::min(hi, std::max(lo, x));
}

/// The cutoff in Hz, limited to [1, 20000] and below Nyquist; this also
/// keeps it >= 0 as the formulas require
inline double cutoffHz(float cutoff, double sampleRate)
{
    return std::min(clamp(cutoff, 1.0, 20000.0), kMaxCutoffPerSampleRate * sampleRate);
}

/// Q of a resonance in dB, limited to [-60, 60] dB
inline double resonanceQ(float resonance)
{
    return std::pow(10.0, clamp(resonance, -60.0, 60.0) / 20.0);
}

/// A = 10^(gain / 40) of a peak or shelf gain in dB, limited to [-120, 60] dB
inline double gainA(float gain)
{
    return std::pow(10.0, clamp(gain, -120.0, 60.0) / 40.0);
}

/// Q of a bandwidth in octaves, limited to [0.01, 12], at the angular
/// frequency w0 (the cookbook's BW case)
inline double bandwidthQ(double w0, float bandwidth)
{
    const double bw = clamp(bandwidth, 0.01, 12.0);
    return 1.0 / (2.0 * std::sinh(0.5 * kLn2 * bw * w0 / std::sin(w0)));
}

/// The cookbook's alpha, with Q kept >= 0.001
inline double alphaOf(double w0, double q)
{
    return std::sin(w0) / (2.0 * std::max(0.001, q));
}

//------------------------------------------------------------------------------
// Audio EQ Cookbook formulas, w0 = 2 pi f0 / Fs, as normalize(b0, b1, b2,
// a0, a1, a2)

inline Coefs lowpass(double w0, double q)
{
    const double cosw0 = std::cos(w0);
    const double alpha = alphaOf(w0, q);
    return normalize(
        (1.0 - cosw0) / 2.0, 1.0 - cosw0, (1.0 - cosw0) / 2.0,
        1.0 + alpha, -2.0 * cosw0, 1.0 - alpha);
}

inline Coefs highpass(double w0, double q)
{
    const double cosw0 = std::cos(w0);
    const double alpha = alphaOf(w0, q);
    return normalize(
        (1.0 + cosw0) / 2.0, -(1.0 + cosw0), (1.0 + cosw0) / 2.0,
        1.0 + alpha, -2.0 * cosw0, 1.0 - alpha);
}

/// Bandpass with a constant peak gain of 0 dB
inline Coefs bandpass(double w0, double q)
{
    const double cosw0 = std::cos(w0);
    const double alpha = alphaOf(w0, q);
    return normalize(
        alpha, 0.0, -alpha,
        1.0 + alpha, -2.0 * cosw0, 1.0 - alpha);
}

inline Coefs notch(double w0, double q)
{
    const double cosw0 = std::cos(w0);
    const double alpha = alphaOf(w0, q);
    return normalize(
        1.0, -2.0 * cosw0, 1.0,
        1.0 + alpha, -2.0 * cosw0, 1.0 - alpha);
}

inline Coefs peakingEq(double w0, double q, double A)
{
    const double cosw0 = std::cos(w0);
    const double alpha = alphaOf(w0, q);
    return normalize(
        1.0 + alpha * A, -2.0 * cosw0, 1.0 - alpha * A,
        1.0 + alpha / A, -2.0 * cosw0, 1.0 - alpha / A);
}

inline Coefs lowShelf(double w0, double q, double A)
{
    const double cosw0 = std::cos(w0);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alphaOf(w0, q);
    return normalize(
        A * ((A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAAlpha),
        2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0),
        A * ((A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAAlpha),
        (A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAAlpha,
        -2.0 * ((A - 1.0) + (A + 1.0) * cosw0),
        (A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAAlpha);
}

inline Coefs highShelf(double w0, double q, double A)
{
    const double cosw0 = std::cos(w0);
    const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alphaOf(w0, q);
    return normalize(
        A * ((A + 1.0) + (A - 1.0) * cosw0 + twoSqrtAAlpha),
        -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0),
        A * ((A + 1.0) + (A - 1.0) * cosw0 - twoSqrtAAlpha),
        (A + 1.0) - (A - 1.0) * cosw0 + twoSqrtAAlpha,
        2.0 * ((A - 1.0) - (A + 1.0) * cosw0),
        (A + 1.0) - (A - 1.0) * cosw0 - twoSqrtAAlpha);
}

//------------------------------------------------------------------------------

/**
   `Sections` identical biquads in cascade on each of `Channels` channels.

   All of them share one set of coefficients. Each coefficient follows its
   target through a one-pole smoother, c = p c + (1 - p) target, with
   p = exp(-1 / (kSmoothingTime Fs)) while smoothing is enabled and p = 0
   otherwise. The coefficients start from 0 after clear().

   Each section is the transposed direct form II of Faust's fi.tf22t, which
   forms the feedback products from the previous output with the current
   coefficients:

       y[n] = b0[n] x[n] + s1 - a1[n] y[n-1]
       s1  <- b1[n] x[n] + s2 - a2[n] y[n-1]
       s2  <- b2[n] x[n]

   The signal stays in double precision from the input to the output.
 */
template <int Channels, int Sections>
class SmoothedBiquads {
public:
    void setSampleRate(double sampleRate)
    {
        fPole = std::exp(-1.0 / (kSmoothingTime * sampleRate));
    }

    void clear()
    {
        fCoefs = Coefs {};
        for (auto& channel : fStates)
            for (State& state : channel)
                state = State {};
    }

    /**
       Filters `count` frames from `inputs` into `outputs`, which may be the
       same buffers, while the coefficients move towards `target`.
     */
    void process(const Coefs& target, bool smooth, int count, const float* const* inputs, float* const* outputs)
    {
        const double pole = smooth ? fPole : 0.0;
        const double gain = 1.0 - pole;
        const Coefs step { gain * target.b0, gain * target.b1, gain * target.b2, gain * target.a1, gain * target.a2 };
        Coefs c = fCoefs;

        for (int i = 0; i < count; ++i) {
            c.b0 = pole * c.b0 + step.b0;
            c.b1 = pole * c.b1 + step.b1;
            c.b2 = pole * c.b2 + step.b2;
            c.a1 = pole * c.a1 + step.a1;
            c.a2 = pole * c.a2 + step.a2;

            double x[Channels];
            for (int ch = 0; ch < Channels; ++ch)
                x[ch] = inputs[ch][i];

            for (int ch = 0; ch < Channels; ++ch) {
                for (State& s : fStates[ch]) {
                    const double y = c.b0 * x[ch] + s.s1 - c.a1 * s.y1;
                    s.s1 = c.b1 * x[ch] + s.s2 - c.a2 * s.y1;
                    s.s2 = c.b2 * x[ch];
                    s.y1 = y;
                    x[ch] = y;
                }
                outputs[ch][i] = static_cast<float>(x[ch]);
            }
        }

        fCoefs = c;
    }

private:
    struct State {
        double s1 = 0.0;
        double s2 = 0.0;
        double y1 = 0.0;
    };

    double fPole = 0.0;
    Coefs fCoefs {};
    State fStates[Channels][Sections];
};

} // namespace rbj
} // namespace sfz

/**
   Base of the filters below: the parameters, with the defaults of the
   generated classes, and the biquads. Like the generated classes, it designs
   the target coefficients from the parameters once per compute().
 */
template <int Channels, int Sections>
class sfzRbjFilter : public sfzFilterDsp {
public:
    void init(int sampleRate) override
    {
        fSampleRate = sampleRate;
        fTwoPiOverSampleRate = sfz::rbj::kTwoPi / sampleRate;
        fBiquads.setSampleRate(sampleRate);
        fParams = Params {};
        instanceClear();
    }

    void instanceClear() override
    {
        fBiquads.clear();
    }

    void compute(int count, const float* const* inputs, float* const* outputs) override
    {
        const double w0 = fTwoPiOverSampleRate * sfz::rbj::cutoffHz(fParams.cutoff, fSampleRate);
        fBiquads.process(design(fParams, w0), fSmoothEnable, count, inputs, outputs);
    }

    float getCutoff() const { return fParams.cutoff; }
    void setCutoff(float value) { fParams.cutoff = value; }

protected:
    struct Params {
        float cutoff = 440.0f; // Hz
        float resonance = 0.0f; // dB
        float gain = 0.0f; // dB, of peaks and shelves
        float bandwidth = 1.0f; // octaves, of equalizer bands
    };

    /// The target coefficients for `params` at the angular cutoff frequency w0
    virtual sfz::rbj::Coefs design(const Params& params, double w0) const = 0;

    Params fParams;

private:
    double fSampleRate = 0.0;
    double fTwoPiOverSampleRate = 0.0;
    sfz::rbj::SmoothedBiquads<Channels, Sections> fBiquads;
};

/// Lowpass, highpass, bandpass and notch filters: cutoff and resonance
template <int Channels, int Sections, sfz::rbj::Coefs (*Formula)(double w0, double q)>
class sfzRbjResonant : public sfzRbjFilter<Channels, Sections> {
    using Params = typename sfzRbjFilter<Channels, Sections>::Params;

public:
    float getResonance() const { return this->fParams.resonance; }
    void setResonance(float value) { this->fParams.resonance = value; }

private:
    sfz::rbj::Coefs design(const Params& params, double w0) const override
    {
        return Formula(w0, sfz::rbj::resonanceQ(params.resonance));
    }
};

/// Peaking and shelving filters: cutoff, resonance and gain
template <int Channels, sfz::rbj::Coefs (*Formula)(double w0, double q, double A)>
class sfzRbjPeakShelf : public sfzRbjFilter<Channels, 1> {
    using Params = typename sfzRbjFilter<Channels, 1>::Params;

public:
    float getResonance() const { return this->fParams.resonance; }
    void setResonance(float value) { this->fParams.resonance = value; }
    float getPeakShelfGain() const { return this->fParams.gain; }
    void setPeakShelfGain(float value) { this->fParams.gain = value; }

private:
    sfz::rbj::Coefs design(const Params& params, double w0) const override
    {
        return Formula(w0, sfz::rbj::resonanceQ(params.resonance), sfz::rbj::gainA(params.gain));
    }
};

/// Equalizer bands: cutoff, gain and bandwidth
template <int Channels, sfz::rbj::Coefs (*Formula)(double w0, double q, double A)>
class sfzRbjEq : public sfzRbjFilter<Channels, 1> {
    using Params = typename sfzRbjFilter<Channels, 1>::Params;

public:
    float getPeakShelfGain() const { return this->fParams.gain; }
    void setPeakShelfGain(float value) { this->fParams.gain = value; }
    float getBandwidth() const { return this->fParams.bandwidth; }
    void setBandwidth(float value) { this->fParams.bandwidth = value; }

private:
    sfz::rbj::Coefs design(const Params& params, double w0) const override
    {
        return Formula(w0, sfz::rbj::bandwidthQ(w0, params.bandwidth), sfz::rbj::gainA(params.gain));
    }
};

class faustLpf2p : public sfzRbjResonant<1, 1, sfz::rbj::lowpass> {};
class faustLpf4p : public sfzRbjResonant<1, 2, sfz::rbj::lowpass> {};
class faustLpf6p : public sfzRbjResonant<1, 3, sfz::rbj::lowpass> {};
class faustHpf2p : public sfzRbjResonant<1, 1, sfz::rbj::highpass> {};
class faustHpf4p : public sfzRbjResonant<1, 2, sfz::rbj::highpass> {};
class faustHpf6p : public sfzRbjResonant<1, 3, sfz::rbj::highpass> {};
class faustBpf2p : public sfzRbjResonant<1, 1, sfz::rbj::bandpass> {};
class faustBpf4p : public sfzRbjResonant<1, 2, sfz::rbj::bandpass> {};
class faustBpf6p : public sfzRbjResonant<1, 3, sfz::rbj::bandpass> {};
class faustBrf2p : public sfzRbjResonant<1, 1, sfz::rbj::notch> {};
class faustLsh : public sfzRbjPeakShelf<1, sfz::rbj::lowShelf> {};
class faustHsh : public sfzRbjPeakShelf<1, sfz::rbj::highShelf> {};
class faustPeq : public sfzRbjPeakShelf<1, sfz::rbj::peakingEq> {};
class faustEqPeak : public sfzRbjEq<1, sfz::rbj::peakingEq> {};
class faustEqLshelf : public sfzRbjEq<1, sfz::rbj::lowShelf> {};
class faustEqHshelf : public sfzRbjEq<1, sfz::rbj::highShelf> {};

class faust2chLpf2p : public sfzRbjResonant<2, 1, sfz::rbj::lowpass> {};
class faust2chLpf4p : public sfzRbjResonant<2, 2, sfz::rbj::lowpass> {};
class faust2chLpf6p : public sfzRbjResonant<2, 3, sfz::rbj::lowpass> {};
class faust2chHpf2p : public sfzRbjResonant<2, 1, sfz::rbj::highpass> {};
class faust2chHpf4p : public sfzRbjResonant<2, 2, sfz::rbj::highpass> {};
class faust2chHpf6p : public sfzRbjResonant<2, 3, sfz::rbj::highpass> {};
class faust2chBpf2p : public sfzRbjResonant<2, 1, sfz::rbj::bandpass> {};
class faust2chBpf4p : public sfzRbjResonant<2, 2, sfz::rbj::bandpass> {};
class faust2chBpf6p : public sfzRbjResonant<2, 3, sfz::rbj::bandpass> {};
class faust2chBrf2p : public sfzRbjResonant<2, 1, sfz::rbj::notch> {};
class faust2chLsh : public sfzRbjPeakShelf<2, sfz::rbj::lowShelf> {};
class faust2chHsh : public sfzRbjPeakShelf<2, sfz::rbj::highShelf> {};
class faust2chPeq : public sfzRbjPeakShelf<2, sfz::rbj::peakingEq> {};
class faust2chEqPeak : public sfzRbjEq<2, sfz::rbj::peakingEq> {};
class faust2chEqLshelf : public sfzRbjEq<2, sfz::rbj::lowShelf> {};
class faust2chEqHshelf : public sfzRbjEq<2, sfz::rbj::highShelf> {};

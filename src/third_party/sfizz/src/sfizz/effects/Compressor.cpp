// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 27 of aud_audio_pm): the
// stereo link (comp_stlink) follows every frame of the right channel, not
// its second frame only. Ticket 28: a silent block passes as it is while
// the compressor is at rest - its detectors silent, its gains at 0 dB and
// the memory of its oversampling filters silent -, which processing it
// would leave as it is; every SFZ file of an app limits its node with this
// effect, idle nodes too. The oversampling filters and the detectors of
// both channels run in one loop each, on state in registers, with the
// operations of the original per sample: the output is the same.
// Release 0.3.2: after a silent block the detectors settle a level below
// 1e-9 and a gain within 1e-7 dB of 0 dB to 0, and the oversampling filters
// a memory below 1e-9, so that the compressor comes to rest about a second
// after a loud sound instead of after seconds; a block with sound keeps its
// state as it is. A silent block whose filters are silent and whose level
// lies below the threshold passes as silence while only the detectors
// decay, as they would in the loop. The output moves by less than 1e-6.

/*
   Note(jpc): implementation status

- [x] comp_gain           Gain (dB)
- [x] comp_attack         Attack time (s)
- [x] comp_release        Release time (s)
- [x] comp_ratio          Ratio (linear gain)
- [x] comp_threshold      Threshold (dB)
- [x] comp_stlink         Stereo link (boolean)

*/

#include "Compressor.h"
#include "gen/compressor.hxx"
#include "Opcode.h"
#include "AudioSpan.h"
#include "MathHelpers.h"
#include "OversamplerHelpers.h"
#include "absl/memory/memory.h"
#include <cmath>
#include <cstring>

static constexpr int _oversampling = 2;

namespace {

// The process_block() of the oversampling filters of both channels in one
// loop, on copies the compiler keeps in registers: the filters are chains
// whose every sample waits for the last, and the two chains overlap.
// Every sample takes the operations of process_block() (Audanika, ticket 28).
template <class Upsampler>
void upsampleStereo(Upsampler (&upsamplers)[sfz::EffectChannels], float* left2x, float* right2x,
    const float* const inputs[], unsigned nframes)
{
    Upsampler left = upsamplers[0];
    Upsampler right = upsamplers[1];
    for (unsigned i = 0; i < nframes; ++i) {
        left.process_sample(left2x[2 * i], left2x[2 * i + 1], inputs[0][i]);
        right.process_sample(right2x[2 * i], right2x[2 * i + 1], inputs[1][i]);
    }
    upsamplers[0] = left;
    upsamplers[1] = right;
}

template <class Downsampler>
void downsampleStereo(Downsampler (&downsamplers)[sfz::EffectChannels], float* const outputs[],
    const float* left2x, const float* right2x, unsigned nframes)
{
    Downsampler left = downsamplers[0];
    Downsampler right = downsamplers[1];
    for (unsigned i = 0; i < nframes; ++i) {
        outputs[0][i] = left.process_sample(left2x + 2 * i);
        outputs[1][i] = right.process_sample(right2x + 2 * i);
    }
    downsamplers[0] = left;
    downsamplers[1] = right;
}

// The floors below which a silent block settles the state to 0 (Audanika,
// ticket 28): the linear level of the detectors and the memory of the
// oversampling filters, and the gain in dB.
constexpr float kLevelFloor = 1e-9f;
constexpr float kFilterFloor = 1e-9f;
constexpr float kGainFloorDb = 1e-7f;

// Copies the input to the output, unless they are the same buffers.
void passThrough(const float* const inputs[], float* const outputs[], unsigned nframes)
{
    for (unsigned c = 0; c < sfz::EffectChannels; ++c) {
        if (outputs[c] != inputs[c])
            std::memcpy(outputs[c], inputs[c], nframes * sizeof(float));
    }
}

} // namespace

namespace sfz {
namespace fx {

    struct Compressor::Impl {
        faustCompressor _compressor[2];
        bool _stlink { Default::compSTLink };
        float _inputGain { Default::compGain };
        AudioBuffer<float, 2> _tempBuffer2x { 2, _oversampling * config::defaultSamplesPerBlock };
        AudioBuffer<float, 2> _gain2x { 2, _oversampling * config::defaultSamplesPerBlock };
        hiir::Downsampler2x<12> _downsampler2x[EffectChannels];
        hiir::Upsampler2x<12> _upsampler2x[EffectChannels];

        // Whether the memory of every oversampling filter is silent.
        bool filtersCleared() const;
        // Decays the detectors the stereo link uses by `count` silent samples
        // of the oversampled rate, as the loop would, if the filters are
        // silent and every detector can (faustCompressor::canDecaySilence);
        // otherwise returns false and changes nothing (Audanika, ticket 28).
        bool decaySilence(int count);
        // Sets the state that lies below the floors to 0 (Audanika, ticket
        // 28).
        void settle();
    };

    bool Compressor::Impl::filtersCleared() const
    {
        for (unsigned c = 0; c < EffectChannels; ++c) {
            if (!_upsampler2x[c].is_cleared() || !_downsampler2x[c].is_cleared())
                return false;
        }
        return true;
    }

    bool Compressor::Impl::decaySilence(int count)
    {
        if (!std::isfinite(_inputGain) || !filtersCleared())
            return false;

        // The stereo link feeds the first detector only.
        const unsigned used = _stlink ? 1 : EffectChannels;
        faustCompressor::Slow coefficients[EffectChannels];
        for (unsigned c = 0; c < used; ++c) {
            coefficients[c] = _compressor[c].slow();
            if (!_compressor[c].canDecaySilence(coefficients[c]))
                return false;
        }

        if (used == 1)
            _compressor[0].decaySilence(coefficients[0], count);
        else
            faustCompressor::decaySilenceTwo(_compressor[0], coefficients[0],
                _compressor[1], coefficients[1], count);
        return true;
    }

    void Compressor::Impl::settle()
    {
        for (faustCompressor& comp : _compressor)
            comp.settle(kLevelFloor, kGainFloorDb);

        for (unsigned c = 0; c < EffectChannels; ++c) {
            _upsampler2x[c].settle(kFilterFloor);
            _downsampler2x[c].settle(kFilterFloor);
        }
    }

    Compressor::Compressor()
        : _impl(new Impl)
    {
        Impl& impl = *_impl;
        for (faustCompressor& comp : impl._compressor)
            comp.instanceResetUserInterface();
    }

    Compressor::~Compressor()
    {
    }

    void Compressor::setSampleRate(double sampleRate)
    {
        Impl& impl = *_impl;
        for (faustCompressor& comp : impl._compressor) {
            comp.classInit(_oversampling * sampleRate);
            comp.instanceConstants(_oversampling * sampleRate);
        }

        for (unsigned c = 0; c < EffectChannels; ++c) {
            impl._downsampler2x[c].set_coefs(OSCoeffs2x);
            impl._upsampler2x[c].set_coefs(OSCoeffs2x);
        }

        clear();
    }

    void Compressor::setSamplesPerBlock(int samplesPerBlock)
    {
        Impl& impl = *_impl;
        impl._tempBuffer2x.resize(_oversampling * samplesPerBlock);
        impl._gain2x.resize(_oversampling * samplesPerBlock);
    }

    void Compressor::clear()
    {
        Impl& impl = *_impl;
        for (faustCompressor& comp : impl._compressor)
            comp.instanceClear();
    }

    bool Compressor::isAtRest() const
    {
        const Impl& impl = *_impl;
        if (!std::isfinite(impl._inputGain))
            return false;

        for (const faustCompressor& comp : impl._compressor) {
            if (!comp.isAtRest())
                return false;
        }

        return impl.filtersCleared();
    }

    void Compressor::process(const float* const inputs[], float* const outputs[], unsigned nframes)
    {
        Impl& impl = *_impl;

        // Silence in, at rest: the output is silent, the state stays.
        const bool silent = isSilent(inputs, nframes);
        if (silent && isAtRest()) {
            passThrough(inputs, outputs, nframes);
            return;
        }

        // Silence in, the filters silent, the level below the threshold: the
        // output is silent, and the detectors decay as in the loop.
        if (silent && impl.decaySilence(_oversampling * nframes)) {
            passThrough(inputs, outputs, nframes);
            impl.settle();
            return;
        }

        auto inOut2x = AudioSpan<float>(impl._tempBuffer2x).first(_oversampling * nframes);

        absl::Span<float> left2x = inOut2x.getSpan(0);
        absl::Span<float> right2x = inOut2x.getSpan(1);

        upsampleStereo(impl._upsampler2x, left2x.data(), right2x.data(), inputs, nframes);

        const float inputGain = impl._inputGain;
        for (unsigned i = 0; i < _oversampling * nframes; ++i) {
            left2x[i] *= inputGain;
            right2x[i] *= inputGain;
        }

        if (!impl._stlink) {
            absl::Span<float> leftGain2x = impl._gain2x.getSpan(0);
            absl::Span<float> rightGain2x = impl._gain2x.getSpan(1);

            faustCompressor::computeTwo(impl._compressor[0], impl._compressor[1],
                _oversampling * nframes, left2x.data(), right2x.data(),
                leftGain2x.data(), rightGain2x.data());

            for (unsigned i = 0; i < _oversampling * nframes; ++i) {
                left2x[i] *= leftGain2x[i];
                right2x[i] *= rightGain2x[i];
            }
        }
        else {
            absl::Span<float> compIn2x = impl._gain2x.getSpan(0);
            for (unsigned i = 0; i < _oversampling * nframes; ++i)
                compIn2x[i] = std::abs(left2x[i]) + std::abs(right2x[i]);

            absl::Span<float> gain2x = impl._gain2x.getSpan(1);

            {
                faustCompressor& comp = impl._compressor[0];
                float* inputs[] = { compIn2x.data() };
                float* outputs[] = { gain2x.data() };
                comp.compute(_oversampling * nframes, inputs, outputs);
            }

            for (unsigned i = 0; i < _oversampling * nframes; ++i) {
                left2x[i] *= gain2x[i];
                right2x[i] *= gain2x[i];
            }
        }

        downsampleStereo(impl._downsampler2x, outputs, left2x.data(), right2x.data(), nframes);

        // After silence, what is left below the floors goes to 0; a block
        // with sound keeps its state as the original does.
        if (silent)
            impl.settle();
    }

    std::unique_ptr<Effect> Compressor::makeInstance(absl::Span<const Opcode> members)
    {
        Compressor* compressor = new Compressor;
        std::unique_ptr<Effect> fx { compressor };

        Impl& impl = *compressor->_impl;

        for (const Opcode& opc : members) {
            switch (opc.lettersOnlyHash) {
            case hash("comp_attack"):
                {
                    auto value = opc.read(Default::compAttack);
                    for (faustCompressor& comp : impl._compressor)
                        comp.setAttack(value);
                }
                break;
            case hash("comp_release"):
                {
                    auto value = opc.read(Default::compRelease);
                    for (faustCompressor& comp : impl._compressor)
                        comp.setRelease(value);
                }
                break;
            case hash("comp_threshold"):
                {
                    auto value = opc.read(Default::compThreshold);
                    for (faustCompressor& comp : impl._compressor)
                        comp.setThreshold(value);
                }
                break;
            case hash("comp_ratio"):
                {
                    auto value = opc.read(Default::compRatio);
                    for (faustCompressor& comp : impl._compressor)
                        comp.setRatio(value);
                }
                break;
            case hash("comp_gain"):
                impl._inputGain = opc.read(Default::compGain);
                break;
            case hash("comp_stlink"):
                impl._stlink = opc.read(Default::compSTLink);
                break;
            }
        }

        return fx;
    }

} // namespace fx
} // namespace sfz

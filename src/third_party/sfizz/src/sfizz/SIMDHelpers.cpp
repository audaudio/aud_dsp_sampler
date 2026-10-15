// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 25 of aud_audio_pm): the
// dispatch table of the SIMD helpers is set once per process.
// Ticket 30: stepSum() sums a step frame by frame as a loop does, mostly in
// runs a vector computes.

#include "SIMDHelpers.h"
#include "SIMDConfig.h"
#include "utility/Debug.h"
#include "simd/HelpersSSE.h"
#include "simd/HelpersAVX.h"
#include "cpuid/cpuinfo.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>

namespace sfz {

template <class T>
struct SIMDDispatch {
    constexpr SIMDDispatch() = default;
    void resetStatus();
    bool getStatus(SIMDOps op) const;
    void setStatus(SIMDOps op, bool enable);

    decltype(&writeInterleavedScalar<T>) writeInterleaved = &writeInterleavedScalar<T>;
    decltype(&readInterleavedScalar<T>) readInterleaved = &readInterleavedScalar<T>;
    decltype(&gainScalar<T>) gain = &gainScalar<T>;
    decltype(&gain1Scalar<T>) gain1 = &gain1Scalar<T>;
    decltype(&divideScalar<T>) divide = &divideScalar<T>;
    decltype(&multiplyAddScalar<T>) multiplyAdd = &multiplyAddScalar<T>;
    decltype(&multiplyAdd1Scalar<T>) multiplyAdd1 = &multiplyAdd1Scalar<T>;
    decltype(&multiplyMulScalar<T>) multiplyMul = &multiplyMulScalar<T>;
    decltype(&multiplyMul1Scalar<T>) multiplyMul1 = &multiplyMul1Scalar<T>;
    decltype(&linearRampScalar<T>) linearRamp = &linearRampScalar<T>;
    decltype(&multiplicativeRampScalar<T>) multiplicativeRamp = &multiplicativeRampScalar<T>;
    decltype(&addScalar<T>) add = &addScalar<T>;
    decltype(&add1Scalar<T>) add1 = &add1Scalar<T>;
    decltype(&subtractScalar<T>) subtract = &subtractScalar<T>;
    decltype(&subtract1Scalar<T>) subtract1 = &subtract1Scalar<T>;
    decltype(&copyScalar<T>) copy = &copyScalar<T>;
    decltype(&cumsumScalar<T>) cumsum = &cumsumScalar<T>;
    decltype(&diffScalar<T>) diff = &diffScalar<T>;
    decltype(&meanScalar<T>) mean = &meanScalar<T>;
    decltype(&sumSquaresScalar<T>) sumSquares = &sumSquaresScalar<T>;
    decltype(&clampAllScalar<T>) clampAll = &clampAllScalar<T>;
    decltype(&allWithinScalar<T>) allWithin = &allWithinScalar<T>;

private:
    std::array<bool, static_cast<unsigned>(SIMDOps::_sentinel)> simdStatus;
    cpuid::cpuinfo info;
};


template <>
bool SIMDDispatch<float>::getStatus(SIMDOps op) const
{
    const unsigned index = static_cast<unsigned>(op);
    ASSERT(index < simdStatus.size());
    return simdStatus[index];
}

template <>
void SIMDDispatch<float>::setStatus(SIMDOps op, bool enable)
{
    const unsigned index = static_cast<unsigned>(op);
    ASSERT(index < simdStatus.size());
    simdStatus[index] = enable;

    if (!enable) {
#define SIMD_OP(opname) case SIMDOps::opname : (opname) = opname ## Scalar<float>; return;
        switch (op) {
            default: break;
            SIMD_OP(writeInterleaved)
            SIMD_OP(readInterleaved)
            SIMD_OP(gain)
            SIMD_OP(gain1)
            SIMD_OP(divide)
            SIMD_OP(linearRamp)
            SIMD_OP(multiplicativeRamp)
            SIMD_OP(add)
            SIMD_OP(add1)
            SIMD_OP(subtract)
            SIMD_OP(subtract1)
            SIMD_OP(multiplyAdd)
            SIMD_OP(multiplyAdd1)
            SIMD_OP(multiplyMul)
            SIMD_OP(multiplyMul1)
            SIMD_OP(copy)
            SIMD_OP(cumsum)
            SIMD_OP(diff)
            SIMD_OP(mean)
            SIMD_OP(sumSquares)
            SIMD_OP(clampAll)
            SIMD_OP(allWithin)
        }
#undef SIMD_OP
    }

#if SFIZZ_CPU_FAMILY_X86_64 || SFIZZ_CPU_FAMILY_I386
#define SIMD_OP(opname) case SIMDOps::opname : (opname) = opname ## AVX; return;
    if (info.has_avx()) {
        switch (op) {
            default: break;
        }
    }
#undef SIMD_OP

#define SIMD_OP(opname) case SIMDOps::opname : (opname) = opname ## SSE; return;
    if (info.has_sse()) {
        switch (op) {
            default: break;
            SIMD_OP(writeInterleaved)
            SIMD_OP(readInterleaved)
            SIMD_OP(gain)
            SIMD_OP(gain1)
            SIMD_OP(divide)
            SIMD_OP(linearRamp)
            SIMD_OP(multiplicativeRamp)
            SIMD_OP(add)
            SIMD_OP(add1)
            SIMD_OP(subtract)
            SIMD_OP(subtract1)
            SIMD_OP(multiplyAdd)
            SIMD_OP(multiplyAdd1)
            SIMD_OP(multiplyMul)
            SIMD_OP(multiplyMul1)
            SIMD_OP(copy)
            SIMD_OP(cumsum)
            SIMD_OP(diff)
            SIMD_OP(mean)
            SIMD_OP(sumSquares)
            SIMD_OP(clampAll)
            SIMD_OP(allWithin)
        }
    }
#undef SIMD_OP
#endif // SFIZZ_CPU_FAMILY_X86_64 || SFIZZ_CPU_FAMILY_I386

#if SFIZZ_CPU_FAMILY_AARCH64 || SFIZZ_CPU_FAMILY_ARM
#define SIMD_OP(opname) case SIMDOps::opname : (opname) = opname ## NEON; return;
    if (info.has_neon()) {
        switch (op) {
            default: break;
        }
    }
#undef SIMD_OP
#endif // SFIZZ_CPU_FAMILY_AARCH64 || SFIZZ_CPU_FAMILY_ARM
}

template <>
void SIMDDispatch<float>::resetStatus()
{
    setStatus(SIMDOps::writeInterleaved, false);
    setStatus(SIMDOps::readInterleaved, false);
    setStatus(SIMDOps::fill, true);
    setStatus(SIMDOps::gain, true);
    setStatus(SIMDOps::gain1, true);
    setStatus(SIMDOps::divide, false);
    setStatus(SIMDOps::linearRamp, false);
    setStatus(SIMDOps::multiplicativeRamp, true);
    setStatus(SIMDOps::add, false);
    setStatus(SIMDOps::add1, false);
    setStatus(SIMDOps::subtract, false);
    setStatus(SIMDOps::subtract1, false);
    setStatus(SIMDOps::multiplyAdd, false);
    setStatus(SIMDOps::multiplyAdd1, false);
    setStatus(SIMDOps::multiplyMul, false);
    setStatus(SIMDOps::multiplyMul1, false);
    setStatus(SIMDOps::copy, false);
    setStatus(SIMDOps::cumsum, true);
    setStatus(SIMDOps::diff, false);
    setStatus(SIMDOps::sfzInterpolationCast, true);
    setStatus(SIMDOps::mean, false);
    setStatus(SIMDOps::sumSquares, false);
    setStatus(SIMDOps::upsampling, true);
    setStatus(SIMDOps::clampAll, false);
    setStatus(SIMDOps::allWithin, true);
}

///

template<class T>
static SIMDDispatch<T>& simdDispatch()
{
    static SIMDDispatch<T> dispatch;
    return dispatch;
}

template<>
void resetSIMDOpStatus<float>()
{
    simdDispatch<float>().resetStatus();
}

template<>
void setSIMDOpStatus<float>(SIMDOps op, bool status)
{
    simdDispatch<float>().setStatus(op, status);
}

template<>
bool getSIMDOpStatus<float>(SIMDOps op)
{
    return simdDispatch<float>().getStatus(op);
}

// The dispatch table is set once per process: every synth asks for it, also
// on a worker thread while other synths render and read it.
void initializeSIMDDispatchers()
{
    static const bool initialized = [] {
        simdDispatch<float>().resetStatus();
        return true;
    }();
    (void)initialized;
}

///

void readInterleaved(const float* input, float* outputLeft, float* outputRight, unsigned inputSize) noexcept
{
    return simdDispatch<float>().readInterleaved(input, outputLeft, outputRight, inputSize);
}

void writeInterleaved(const float* inputLeft, const float* inputRight, float* output, unsigned outputSize) noexcept
{
    return simdDispatch<float>().writeInterleaved(inputLeft, inputRight, output, outputSize);
}

template <>
void applyGain1<float>(float gain, const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().gain1(gain, input, output, size);
}

template <>
void applyGain<float>(const float* gain, const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().gain(gain, input, output, size);
}

template <>
void divide<float>(const float* input, const float* divisor, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().divide(input, divisor, output, size);
}

template <>
void multiplyAdd<float>(const float* gain, const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().multiplyAdd(gain, input, output, size);
}

template <>
void multiplyAdd1<float>(float gain, const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().multiplyAdd1(gain, input, output, size);
}

template <>
void multiplyMul<float>(const float* gain, const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().multiplyMul(gain, input, output, size);
}

template <>
void multiplyMul1<float>(float gain, const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().multiplyMul1(gain, input, output, size);
}

template <>
float linearRamp<float>(float* output, float start, float step, unsigned size) noexcept
{
    return simdDispatch<float>().linearRamp(output, start, step, size);
}

template <>
float multiplicativeRamp<float>(float* output, float start, float step, unsigned size) noexcept
{
    return simdDispatch<float>().multiplicativeRamp(output, start, step, size);
}

template <>
void add<float>(const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().add(input, output, size);
}

template <>
void add1<float>(float value, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().add1(value, output, size);
}

template <>
void subtract<float>(const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().subtract(input, output, size);
}

template <>
void subtract1<float>(float value, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().subtract1(value, output, size);
}

template <>
void copy<float>(const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().copy(input, output, size);
}

template <>
float mean<float>(const float* vector, unsigned size) noexcept
{
    return simdDispatch<float>().mean(vector, size);
}

template <>
float sumSquares<float>(const float* vector, unsigned size) noexcept
{
    return simdDispatch<float>().sumSquares(vector, size);
}

template <>
void cumsum<float>(const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().cumsum(input, output, size);
}

template <>
void diff<float>(const float* input, float* output, unsigned size) noexcept
{
    return simdDispatch<float>().diff(input, output, size);
}

template <>
void clampAll<float>(float* input, float low, float high, unsigned size) noexcept
{
    simdDispatch<float>().clampAll(input, low, high, size);
}

template <>
bool allWithin<float>(const float* input, float low, float high, unsigned size) noexcept
{
    return simdDispatch<float>().allWithin(input, low, high, size);
}

static float floatOfBits(uint32_t bits) noexcept
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void stepSum(float start, float step, float* output, unsigned size) noexcept
{
    if (size == 0)
        return;

    output[0] = start;
    unsigned i = 1;

    // Below 16 steps the binades hold too few sums for a run: a plain loop,
    // as from the start of an envelope's stage or of a voice's positions.
    const float plain = 16.0f * step;
    for (; i < size && output[i - 1] < plain; ++i)
        output[i] = output[i - 1] + step;

    while (i < size) {
        // A step as the loop takes it.
        const float value = output[i - 1] + step;
        output[i++] = value;
        if (i == size)
            break;

        // The binade [top / 2, top) of the value, of a positive value with a
        // normal ulp and of a positive step only.
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint32_t exponent = bits >> 23;
        if (!(step > 0.0f) || !(step < std::numeric_limits<float>::max()) || exponent <= 24 || exponent >= 254)
            continue;
        const float top = floatOfBits((exponent + 1) << 23);
        // Few frames to the top: the loop is quicker.
        if (top - value < 8.0f * step)
            continue;
        const float ulp = floatOfBits((exponent - 23) << 23);

        // The step rounded to the ulp; both differences are exact. Halfway
        // between two multiples of the ulp, a sum rounds to the even one,
        // which changes from sum to sum: the loop goes on.
        const float next = value + step;
        if (!(next < top - ulp))
            continue;
        const float rounded = next - value;
        const float rest = step - rounded;
        if (std::fabs(rest) == 0.5f * ulp)
            continue;

        // The run: the sums up to 2 ulps below the top, where the next sum
        // rounds within the binade; the bound is exact in float, the check
        // of a run in double.
        const float limit = top - 2.0f * ulp;
        unsigned frames = size - i;
        if (rounded > 0.0f)
            frames = static_cast<unsigned>(std::min(static_cast<float>(frames), std::floor((limit - value) / rounded)));
        while (frames > 0 && static_cast<double>(value) + static_cast<double>(frames) * static_cast<double>(rounded) > static_cast<double>(limit))
            --frames;
        // Runs are short at the start of a sum: vectors of 4, not 16.
        float* out = output + i - 1;
#if defined(__clang__)
#pragma clang loop interleave_count(1)
#endif
        for (unsigned j = 1; j <= frames; ++j)
            out[j] = value + static_cast<float>(j) * rounded;
        i += frames;
    }
}

}

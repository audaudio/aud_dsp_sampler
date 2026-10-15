// Modified by Audanika for aud_dsp_sampler (ticket 30 of aud_audio_pm):
// panMono() and panStereo() apply the gain of a voice and a pan, a width and
// a position that hold their values in one pass. They take the gains the
// NEON code of pan() and width() takes on the frames it treats, and those of
// the scalar code on the others, so that the result stays the same bit for
// bit.

#include "Panning.h"
#include "MathHelpers.h"
#include <array>
#include <cmath>

#if SFIZZ_HAVE_NEON
#include <arm_neon.h>
#include "simd/Common.h"
using Type = float;
constexpr unsigned TypeAlignment = 4;
constexpr unsigned ByteAlignment = TypeAlignment * sizeof(Type);
#endif


namespace sfz
{

constexpr int panSize { 4095 };

// Table of pan values for the left channel, extra element for safety
static const auto panData = []()
{
    std::array<float, panSize + 1> pan;
    int i = 0;

    for (; i < panSize; ++i)
        pan[i] = std::cos(i * (piTwo<double>() / (panSize - 1)));

    for (; i < static_cast<int>(pan.size()); ++i)
        pan[i] = pan[panSize - 1];

    return pan;
}();

float panLookup(float pan)
{
    // reduce range, round to nearest
    const long index = lroundPositive(pan * (panSize - 1));
    return panData[index];
}

// The gains of the scalar code for a pan of `value`, and for a width of
// `value`: coeff1, coeff2.
inline void scalarPanGains(float value, float& left, float& right)
{
    auto p = (value + 1.0f) * 0.5f;
    p = clamp(p, 0.0f, 1.0f);
    left = panLookup(p);
    right = panLookup(1 - p);
}

inline void tickPan(const float* pan, float* leftBuffer, float* rightBuffer)
{
    float left;
    float right;
    scalarPanGains(*pan, left, right);
    *leftBuffer *= left;
    *rightBuffer *= right;
}

void pan(const float* panEnvelope, float* leftBuffer, float* rightBuffer, unsigned size) noexcept
{
    const auto* sentinel = panEnvelope + size;

#if SFIZZ_HAVE_NEON
    const auto* firstAligned = prevAligned<ByteAlignment>(panEnvelope + TypeAlignment - 1);

    if (willAlign<ByteAlignment>(panEnvelope, leftBuffer, rightBuffer) && (firstAligned < sentinel)) {
        while (panEnvelope < firstAligned) {
            tickPan(panEnvelope, leftBuffer, rightBuffer);
            incrementAll(panEnvelope, leftBuffer, rightBuffer);
        }

        uint32_t indices[TypeAlignment];
        float leftPan[TypeAlignment];
        float rightPan[TypeAlignment];
        const auto* lastAligned = prevAligned<ByteAlignment>(sentinel);
        while (panEnvelope < lastAligned) {
            float32x4_t mmPan = vld1q_f32(panEnvelope);
            mmPan = vaddq_f32(mmPan, vdupq_n_f32(1.0f));
            mmPan = vmulq_n_f32(mmPan, 0.5f * panSize);
            mmPan = vaddq_f32(mmPan, vdupq_n_f32(0.5f));
            uint32x4_t mmIdx = vcvtq_u32_f32(mmPan);
            mmIdx = vminq_u32(mmIdx, vdupq_n_u32(panSize - 1));
            mmIdx = vmaxq_u32(mmIdx, vdupq_n_u32(0));
            vst1q_u32(indices, mmIdx);

            leftPan[0] = panData[indices[0]];
            rightPan[0] = panData[panSize - indices[0] - 1];
            leftPan[1] = panData[indices[1]];
            rightPan[1] = panData[panSize - indices[1] - 1];
            leftPan[2] = panData[indices[2]];
            rightPan[2] = panData[panSize - indices[2] - 1];
            leftPan[3] = panData[indices[3]];
            rightPan[3] = panData[panSize - indices[3] - 1];

            vst1q_f32(leftBuffer, vmulq_f32(vld1q_f32(leftBuffer), vld1q_f32(leftPan)));
            vst1q_f32(rightBuffer, vmulq_f32(vld1q_f32(rightBuffer), vld1q_f32(rightPan)));

            incrementAll<TypeAlignment>(panEnvelope, leftBuffer, rightBuffer);
        }
    }
#endif

    while (panEnvelope < sentinel) {
        tickPan(panEnvelope, leftBuffer, rightBuffer);
        incrementAll(panEnvelope, leftBuffer, rightBuffer);
    }

}

// The width of a frame in the scalar code; panStereo() shares it, so that
// the compiler contracts its sums the same way for both.
inline void widen(float coeff1, float coeff2, float& left, float& right)
{
    const auto l = left;
    const auto r = right;
    left = l * coeff2 + r * coeff1;
    right = l * coeff1 + r * coeff2;
}

inline void tickWidth(const float* width, float* leftBuffer, float* rightBuffer)
{
    float coeff1;
    float coeff2;
    scalarPanGains(*width, coeff1, coeff2);
    widen(coeff1, coeff2, *leftBuffer, *rightBuffer);
}

void width(const float* widthEnvelope, float* leftBuffer, float* rightBuffer, unsigned size) noexcept
{
    const auto* sentinel = widthEnvelope + size;

#if SFIZZ_HAVE_NEON
    const auto* firstAligned = prevAligned<ByteAlignment>(widthEnvelope + TypeAlignment  - 1);

    if (willAlign<ByteAlignment>(widthEnvelope, leftBuffer, rightBuffer) && firstAligned < sentinel) {
        while (widthEnvelope < firstAligned) {
            tickWidth(widthEnvelope, leftBuffer, rightBuffer);
            incrementAll(widthEnvelope, leftBuffer, rightBuffer);
        }

        uint32_t indices[TypeAlignment];
        float coeff1[TypeAlignment];
        float coeff2[TypeAlignment];
        const auto* lastAligned = prevAligned<ByteAlignment>(sentinel);
        while (widthEnvelope < lastAligned) {
            float32x4_t mmWidth = vld1q_f32(widthEnvelope);
            mmWidth = vaddq_f32(mmWidth, vdupq_n_f32(1.0f));
            mmWidth = vmulq_n_f32(mmWidth, 0.5f * panSize);
            mmWidth = vaddq_f32(mmWidth, vdupq_n_f32(0.5f));
            uint32x4_t mmIdx = vcvtq_u32_f32(mmWidth);
            mmIdx = vminq_u32(mmIdx, vdupq_n_u32(panSize - 1));
            mmIdx = vmaxq_u32(mmIdx, vdupq_n_u32(0));
            vst1q_u32(indices, mmIdx);

            coeff1[0] = panData[indices[0]];
            coeff2[0] = panData[panSize - indices[0] - 1];
            coeff1[1] = panData[indices[1]];
            coeff2[1] = panData[panSize - indices[1] - 1];
            coeff1[2] = panData[indices[2]];
            coeff2[2] = panData[panSize - indices[2] - 1];
            coeff1[3] = panData[indices[3]];
            coeff2[3] = panData[panSize - indices[3] - 1];

            float32x4_t mmCoeff1 = vld1q_f32(coeff1);
            float32x4_t mmCoeff2 = vld1q_f32(coeff2);
            float32x4_t mmLeft = vld1q_f32(leftBuffer);
            float32x4_t mmRight = vld1q_f32(rightBuffer);

            vst1q_f32(leftBuffer, vaddq_f32(vmulq_f32(mmCoeff2, mmLeft), vmulq_f32(mmCoeff1, mmRight)));
            vst1q_f32(rightBuffer, vaddq_f32(vmulq_f32(mmCoeff1, mmLeft), vmulq_f32(mmCoeff2, mmRight)));

            incrementAll<TypeAlignment>(widthEnvelope, leftBuffer, rightBuffer);
        }
    }
#endif // SFIZZ_HAVE_NEON

    while (widthEnvelope < sentinel) {
        tickWidth(widthEnvelope, leftBuffer, rightBuffer);
        incrementAll(widthEnvelope, leftBuffer, rightBuffer);
    }
}

#if SFIZZ_HAVE_NEON
// The frames pan() and width() treat with NEON when their envelope comes
// from the buffer pool, which starts it aligned: those up to the last
// multiple of 4 when both channels start aligned too, else none.
inline unsigned neonFrames(const float* leftBuffer, const float* rightBuffer, unsigned size)
{
    if (unaligned<ByteAlignment>(leftBuffer, rightBuffer))
        return 0;
    return size & ~(TypeAlignment - 1);
}

// The gains the NEON code of pan() takes for a pan of `value`, and those of
// width() for a width of `value`: coeff1, coeff2.
inline void neonPanGains(float value, float& left, float& right)
{
    float32x4_t mmPan = vdupq_n_f32(value);
    mmPan = vaddq_f32(mmPan, vdupq_n_f32(1.0f));
    mmPan = vmulq_n_f32(mmPan, 0.5f * panSize);
    mmPan = vaddq_f32(mmPan, vdupq_n_f32(0.5f));
    uint32x4_t mmIdx = vcvtq_u32_f32(mmPan);
    mmIdx = vminq_u32(mmIdx, vdupq_n_u32(panSize - 1));
    mmIdx = vmaxq_u32(mmIdx, vdupq_n_u32(0));
    const uint32_t index = vgetq_lane_u32(mmIdx, 0);
    left = panData[index];
    right = panData[panSize - index - 1];
}
#endif

void panMono(float pan, float gain, const float* envelope,
    float* leftBuffer, float* rightBuffer, unsigned size) noexcept
{
    // In place on the left, so that the compiler sees the left read and
    // written at the same index and vectorizes.
    const auto apply = [=](unsigned begin, unsigned end, float leftPan, float rightPan) {
        for (unsigned i = begin; i < end; ++i) {
            const float sample = envelope[i] * leftBuffer[i];
            leftBuffer[i] = gain * (sample * leftPan);
            rightBuffer[i] = gain * (sample * rightPan);
        }
    };

    // The frames of the NEON code take its gains, the others those of the
    // scalar code.
    unsigned neon = 0;
#if SFIZZ_HAVE_NEON
    float neonLeft;
    float neonRight;
    neonPanGains(pan, neonLeft, neonRight);
    neon = neonFrames(leftBuffer, rightBuffer, size);
    apply(0, neon, neonLeft, neonRight);
#endif
    float left;
    float right;
    scalarPanGains(pan, left, right);
    apply(neon, size, left, right);
}

void panStereo(float pan, float width, float position, float gain, const float* envelope,
    float* leftBuffer, float* rightBuffer, unsigned size) noexcept
{
    float panLeft;
    float panRight;
    float coeff1;
    float coeff2;
    float positionLeft;
    float positionRight;
    unsigned i = 0;

#if SFIZZ_HAVE_NEON
    // The frames of the NEON code: the products apart from their sums, as
    // its width() computes them, without a fused multiply-add.
    neonPanGains(pan, panLeft, panRight);
    neonPanGains(width, coeff1, coeff2);
    neonPanGains(position, positionLeft, positionRight);
    for (const unsigned neon = neonFrames(leftBuffer, rightBuffer, size); i < neon; ++i) {
        float left = envelope[i] * leftBuffer[i];
        float right = envelope[i] * rightBuffer[i];
        left *= panLeft;
        right *= panRight;
        const float leftOfLeft = coeff2 * left;
        const float leftOfRight = coeff1 * right;
        const float rightOfLeft = coeff1 * left;
        const float rightOfRight = coeff2 * right;
        left = leftOfLeft + leftOfRight;
        right = rightOfLeft + rightOfRight;
        left *= positionLeft;
        right *= positionRight;
        leftBuffer[i] = gain * left;
        rightBuffer[i] = gain * right;
    }
#endif

    // The other frames as the scalar code treats them.
    scalarPanGains(pan, panLeft, panRight);
    scalarPanGains(width, coeff1, coeff2);
    scalarPanGains(position, positionLeft, positionRight);
    for (; i < size; ++i) {
        float left = envelope[i] * leftBuffer[i];
        float right = envelope[i] * rightBuffer[i];
        left *= panLeft;
        right *= panRight;
        widen(coeff1, coeff2, left, right);
        left *= positionLeft;
        right *= positionRight;
        leftBuffer[i] = gain * left;
        rightBuffer[i] = gain * right;
    }
}

}

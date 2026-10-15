// Modified by Audanika for aud_dsp_sampler (ticket 30 of aud_audio_pm):
// panMono() and panStereo() apply the gain of a voice and a pan, a width and
// a position that hold their values in one pass, with the products and the
// gains of the passes they replace.

#pragma once
#include "absl/types/span.h"
#include "MathHelpers.h"

namespace sfz
{

/**
* @brief Lookup a value from the pan table
*        No check is done on the range, needs to be capped
*        between 0 and panSize.
*
* @param pan
* @return float
*/
float panLookup(float pan);


/**
 * @brief Pans a mono signal left or right
 *
 * @param panEnvelope
 * @param leftBuffer
 * @param rightBuffer
 * @param size
 */
void pan(const float* panEnvelope, float* leftBuffer, float* rightBuffer, unsigned size) noexcept;
inline void pan(absl::Span<const float> panEnvelope, absl::Span<float> leftBuffer, absl::Span<float> rightBuffer) noexcept
{
    CHECK_SPAN_SIZES(panEnvelope, leftBuffer, rightBuffer);
    pan(panEnvelope.data(), leftBuffer.data(), rightBuffer.data(), minSpanSize(panEnvelope, leftBuffer, rightBuffer));
}

/**
 * @brief Controls the width of a stereo signal, setting it to mono when width = 0 and inverting the channels
 * when width = -1. Width = 1 has no effect.
 *
 * @param widthEnvelope
 * @param leftBuffer
 * @param rightBuffer
 * @param size
 */
void width(const float* widthEnvelope, float* leftBuffer, float* rightBuffer, unsigned size) noexcept;
inline void width(absl::Span<const float> widthEnvelope, absl::Span<float> leftBuffer, absl::Span<float> rightBuffer) noexcept
{
    CHECK_SPAN_SIZES(widthEnvelope, leftBuffer, rightBuffer);
    width(widthEnvelope.data(), leftBuffer.data(), rightBuffer.data(), minSpanSize(widthEnvelope, leftBuffer, rightBuffer));
}

/**
 * @brief Applies an envelope to the mono signal in `leftBuffer` and pans it
 * with a pan that holds its value, then applies a gain, in one pass
 * (aud_dsp_sampler, ticket 30). The result is that of applyGain() of the
 * envelope, a copy to the right, pan() over an envelope of `pan` from the
 * buffer pool and applyGain1() of `gain` on both channels, bit for bit.
 *
 * @param pan the pan, -1 to 1
 * @param gain the gain after the pan
 * @param envelope the gains of the frames
 * @param leftBuffer the mono signal, then the left channel
 * @param rightBuffer
 * @param size
 */
void panMono(float pan, float gain, const float* envelope,
    float* leftBuffer, float* rightBuffer, unsigned size) noexcept;

/**
 * @brief Applies an envelope to a stereo signal, a pan, a width and a
 * position that hold their values, and a gain, in one pass
 * (aud_dsp_sampler, ticket 30). The result is that of applyGain() of the
 * envelope on both channels, pan(), width() and pan() over envelopes of
 * `pan`, `width` and `position` from the buffer pool, and applyGain1() of
 * `gain` on both channels, bit for bit.
 *
 * @param pan the pan, -1 to 1
 * @param width the width, -1 to 1
 * @param position the position, -1 to 1
 * @param gain the gain after the position
 * @param envelope the gains of the frames
 * @param leftBuffer
 * @param rightBuffer
 * @param size
 */
void panStereo(float pan, float width, float position, float gain, const float* envelope,
    float* leftBuffer, float* rightBuffer, unsigned size) noexcept;

}

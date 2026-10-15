// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 30 of aud_audio_pm): a
// timing can be disabled, so that it reads no clock.

#pragma once

#include <chrono>
#include <type_traits>

namespace sfz
{

using Duration = std::chrono::duration<double>;
using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

static TimePoint highResNow()
{
#ifdef EMSCRIPTEN
    return {};
#else
    return std::chrono::high_resolution_clock::now();
#endif
}

/**
 * @brief Creates an RAII logger which fills or adds to a duration on destruction
 *
 */
struct ScopedTiming
{
    enum class Operation
    {
        addToDuration,
        replaceDuration
    };
    ScopedTiming() = delete;
    /**
     * @brief Construct a new Scoped Logger object
     *
     * @param targetDuration
     * @param op
     */
    ScopedTiming(double& targetDuration, Operation op = Operation::replaceDuration, bool enabled = true)
    : targetDuration(targetDuration), operation(op), enabled(enabled),
      creationTime(enabled ? highResNow() : TimePoint {}) {}
    ~ScopedTiming()
    {
        if (!enabled)
            return;
        switch(operation)
        {
        case(Operation::replaceDuration):
            targetDuration = (highResNow() - creationTime).count();
            break;
        case(Operation::addToDuration):
            targetDuration += (highResNow() - creationTime).count();
            break;
        }
    }

    double& targetDuration;
    const Operation operation;
    const bool enabled;
    const TimePoint creationTime;
};

}

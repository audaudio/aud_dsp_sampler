// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 28 of aud_audio_pm): the
// curves of the shapes, which every synth of the process shares, are guarded
// by a mutex. Synths load on several worker threads at once, and their
// regions asked for and dropped curves of the one map without a lock, which
// corrupted it. Only loads take the lock; a voice reads the curve its point
// holds.

#include "FlexEGDescription.h"
#include "Curve.h"
#include "MidiState.h"
#include <absl/container/flat_hash_map.h>
#include <cmath>
#include <mutex>

namespace sfz {

void FlexEGPoint::setShape(float shape)
{
    shape_ = shape;
    shapeCurve_ = FlexEGs::getShapeCurve(shape);
}

const Curve& FlexEGPoint::curve() const
{
    if (shapeCurve_)
        return *shapeCurve_;
    else
        return Curve::getDefault();
}

///
// The curves of the shapes of the process, held while a point uses them.
struct FlexEGShapes {
    std::mutex mutex;
    absl::flat_hash_map<float, std::weak_ptr<Curve>> curves;
};

// Never destroyed: a load may still run on a worker thread at exit.
static FlexEGShapes& getShapes()
{
    static FlexEGShapes* shapes = new FlexEGShapes();
    return *shapes;
}

std::shared_ptr<Curve> FlexEGs::getShapeCurve(float shape)
{
    FlexEGShapes& shapes = getShapes();
    std::lock_guard<std::mutex> lock { shapes.mutex };

    std::weak_ptr<Curve>& slot = shapes.curves[shape];

    std::shared_ptr<Curve> curve = slot.lock();
    if (curve)
        return curve;

    curve.reset(new Curve);

    ///
    constexpr unsigned numPoints = Curve::NumValues;
    float points[numPoints];

    if (shape == 0)
        *curve = Curve::getDefault();
    else if (shape > 0) {
        for (unsigned i = 0; i < numPoints; ++i) {
            float x = float(i) / (numPoints - 1);
            points[i] = std::pow(x, 1 + shape);
        }
        *curve = Curve::buildFromPoints(points);
    }
    else if (shape < 0) {
        for (unsigned i = 0; i < numPoints; ++i) {
            float x = float(i) / (numPoints - 1);
            points[i] = 1 - std::pow(1 - x, 1 - shape);
        }
        *curve = Curve::buildFromPoints(points);
    }

    ///
    slot = curve;
    return curve;
}

void FlexEGs::clearUnusedCurves()
{
    FlexEGShapes& shapes = getShapes();
    std::lock_guard<std::mutex> lock { shapes.mutex };

    for (auto it = shapes.curves.begin(); it != shapes.curves.end(); ) {
        if (it->second.use_count() == 0)
            shapes.curves.erase(it++);
        else
            ++it;
    }
}

///
float FlexEGPoint::getTime(const MidiState& state, int delay) const noexcept
{
    float returnedValue { time };
    for (const CCData<float>& mod : ccTime)
        returnedValue += state.getCCValueAt(mod.cc, delay) * mod.data;
    return returnedValue;
}

float FlexEGPoint::getLevel(const MidiState& state, int delay) const noexcept
{
    float returnedValue { level };
    for (const CCData<float>& mod : ccLevel)
        returnedValue += state.getCCValueAt(mod.cc, delay) * mod.data;
    return returnedValue;
}

} // namespace sfz

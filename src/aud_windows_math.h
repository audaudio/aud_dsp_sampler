// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Force-included into every unit on Windows (ticket 38). The C runtime of
// Windows defines HUGE_VAL in <math.h>; simde takes that for a sign that
// <cmath> is in and calls std::isnan and its kin without including it.
#ifndef AUD_WINDOWS_MATH_H
#define AUD_WINDOWS_MATH_H

#ifdef __cplusplus
#include <cmath>
#endif

#endif  // AUD_WINDOWS_MATH_H

// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// The assets of an Android app as files of the sampler (ticket 27): a path
// "apk:/<path>" names an asset of the APK, read in place through
// AAssetManager without a copy. On the other platforms the backend does not
// exist and apk:/ paths fail their loads.

#ifndef AUD_APK_ASSETS_HPP
#define AUD_APK_ASSETS_HPP

#include <cstdint>
#include <string>

namespace aud_sfz {

// The scheme of the paths the backend serves.
extern const char* const kApkScheme;

// [control] Registers the assets of `assetManager` (an AAssetManager*) as the
// backend of apk:/ paths; AUD_OK, AUD_ERROR_INVALID_ARGUMENT for NULL,
// AUD_ERROR_UNSUPPORTED off Android.
int32_t setAndroidAssets(void* assetManager);

// Why a virtual path cannot be read on this platform, e.g. an apk:/ path
// without the assets of an Android app; empty when a backend serves it or
// the path is no virtual path.
std::string unavailableReason(const std::string& path);

}  // namespace aud_sfz

#endif  // AUD_APK_ASSETS_HPP

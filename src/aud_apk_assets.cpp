// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

#include "aud_apk_assets.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#endif

#include "aud_abi.h"

#include "VirtualFile.h"
#include "aud_dsp_sampler.h"

namespace aud_sfz {

const char* const kApkScheme = "apk";

#if defined(__ANDROID__)
namespace {

// What an empty asset points to.
const unsigned char kNoBytes = 0;

// The bytes of an asset: for an uncompressed asset the mapping of its part
// of the APK, for a compressed one a buffer Android inflates. Both live as
// long as the asset is open.
class ApkAsset : public sfz::VirtualFileData {
 public:
  // An empty asset is readable without a buffer; one longer than the
  // address space, which only a 32-bit process can meet, is not readable.
  explicit ApkAsset(AAsset* asset) : asset_(asset) {
    const off64_t length = AAsset_getLength64(asset);
    if (length == 0) {
      readable_ = true;
    } else if (length > 0 &&
               static_cast<uint64_t>(length) <= std::numeric_limits<size_t>::max()) {
      data_ = AAsset_getBuffer(asset);
      readable_ = data_ != nullptr;
      if (readable_) size_ = static_cast<size_t>(length);
    }
  }
  ~ApkAsset() override { AAsset_close(asset_); }
  const void* data() const noexcept override { return data_ != nullptr ? data_ : &kNoBytes; }
  size_t size() const noexcept override { return size_; }
  bool readable() const noexcept { return readable_; }

 private:
  AAsset* asset_;
  const void* data_ = nullptr;
  size_t size_ = 0;
  bool readable_ = false;
};

// The assets of the APK below "apk:/". AAssetManager locks itself, so the
// workers of the engine may open assets at the same time.
class ApkAssets : public sfz::VirtualFileBackend {
 public:
  explicit ApkAssets(AAssetManager* manager) : manager_(manager) {}

  int64_t sizeOf(const std::string& path) override {
    AAsset* asset = AAssetManager_open(manager_, path.c_str(), AASSET_MODE_UNKNOWN);
    if (asset == nullptr) return -1;
    const off64_t length = AAsset_getLength64(asset);
    AAsset_close(asset);
    return length < 0 ? 0 : static_cast<int64_t>(length);
  }

  std::unique_ptr<sfz::VirtualFileData> open(const std::string& path) override {
    AAsset* asset = AAssetManager_open(manager_, path.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) return nullptr;
    std::unique_ptr<ApkAsset> data(new ApkAsset(asset));
    if (!data->readable()) return nullptr;
    return std::unique_ptr<sfz::VirtualFileData>(data.release());
  }

  // An asset directory lists its files only, not its subdirectories.
  std::vector<std::string> listFiles(const std::string& directory) override {
    std::vector<std::string> names;
    AAssetDir* dir = AAssetManager_openDir(manager_, directory.c_str());
    if (dir == nullptr) return names;
    for (const char* name = AAssetDir_getNextFileName(dir); name != nullptr;
         name = AAssetDir_getNextFileName(dir)) {
      names.emplace_back(name);
    }
    AAssetDir_close(dir);
    return names;
  }

 private:
  AAssetManager* manager_;
};

}  // namespace
#endif

int32_t setAndroidAssets(void* assetManager) {
#if defined(__ANDROID__)
  if (assetManager == nullptr) return AUD_ERROR_INVALID_ARGUMENT;
  sfz::VirtualFiles::setBackend(
      kApkScheme, std::make_shared<ApkAssets>(static_cast<AAssetManager*>(assetManager)));
  return AUD_OK;
#else
  (void)assetManager;
  return AUD_ERROR_UNSUPPORTED;
#endif
}

std::string unavailableReason(const std::string& path) {
  if (!sfz::VirtualFiles::isVirtual(path) || sfz::VirtualFiles::backendOf(path)) return "";
  const std::string scheme = sfz::VirtualFiles::schemeOf(path);
  if (scheme == kApkScheme) {
    return "apk:/ paths read the assets of an Android app, which this platform has none of: " +
           path;
  }
  return "no backend serves " + scheme + ":/ paths on this platform: " + path;
}

}  // namespace aud_sfz

AUD_EXPORT int32_t aud_dsp_sampler_set_android_assets(void* asset_manager) {
  return aud_sfz::setAndroidAssets(asset_manager);
}

#if defined(__ANDROID__)
// Called by the content provider of the package's Android plugin when the
// app process starts, with the AssetManager of the application context. The
// global reference keeps the Java object, and with it the AAssetManager,
// alive for the process.
extern "C" JNIEXPORT void JNICALL
Java_com_audanika_aud_1dsp_1sampler_AudDspSamplerAssets_nativeSetAssetManager(JNIEnv* env, jclass,
                                                                               jobject manager) {
  if (env == nullptr || manager == nullptr) return;
  jobject kept = env->NewGlobalRef(manager);
  if (kept == nullptr) return;
  if (aud_sfz::setAndroidAssets(AAssetManager_fromJava(env, kept)) != AUD_OK) {
    env->DeleteGlobalRef(kept);
  }
}
#endif

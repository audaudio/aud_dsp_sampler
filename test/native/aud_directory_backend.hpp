// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// A backend of virtual files for the tests (ticket 27): serves a folder of
// the host as the assets of an APK, so that "apk:/" paths are tested on the
// host without an Android device. Like an APK it lists the files of a
// directory, not its subdirectories.
//
// Includes VirtualFile.h, which reads the same in every language version:
// the tests of the fork compile as gnu++14, the others as C++17.

#ifndef AUD_DIRECTORY_BACKEND_HPP
#define AUD_DIRECTORY_BACKEND_HPP

#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "VirtualFile.h"
// ghc's filesystem in every language version, not std::filesystem, so that
// the inline functions read the same in both; the folders of every host,
// Windows among them (ticket 38).
#include "ghc/filesystem.hpp"

namespace aud_sampler_test {

namespace gfs = ghc::filesystem;

// The bytes of a file of the host, loaded at once.
class DirectoryFileData : public sfz::VirtualFileData {
 public:
  explicit DirectoryFileData(std::vector<char> bytes) : bytes_(std::move(bytes)) {}
  const void* data() const noexcept override { return bytes_.data(); }
  size_t size() const noexcept override { return bytes_.size(); }

 private:
  std::vector<char> bytes_;
};

// The files below `root` as a backend.
class DirectoryBackend : public sfz::VirtualFileBackend {
 public:
  explicit DirectoryBackend(std::string root) : root_(std::move(root)) {}

  // The opens of the backend and their paths, for the tests of what reads
  // through it.
  int opens = 0;
  std::vector<std::string> openedPaths;

  // The host's filesystem may ignore the case of a name, an APK does not:
  // every component of the path must be spelled as the folder has it.
  int64_t sizeOf(const std::string& path) override {
    if (!spelledAsStored(path)) return -1;
    std::error_code error;
    if (!gfs::is_regular_file(fileOf(path), error)) return -1;
    const auto size = gfs::file_size(fileOf(path), error);
    return error ? -1 : static_cast<int64_t>(size);
  }

  std::unique_ptr<sfz::VirtualFileData> open(const std::string& path) override {
    if (sizeOf(path) < 0) return nullptr;
    opens += 1;
    openedPaths.push_back(path);
    std::ifstream in(fileOf(path), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return std::unique_ptr<sfz::VirtualFileData>(new DirectoryFileData(std::move(bytes)));
  }

  std::vector<std::string> listFiles(const std::string& directory) override {
    std::vector<std::string> names;
    if (!spelledAsStored(directory)) return names;
    std::error_code error;
    for (gfs::directory_iterator entry(fileOf(directory), error), end;
         !error && entry != end; entry.increment(error)) {
      if (entry->is_regular_file(error)) {
        names.push_back(entry->path().filename().string());
      }
    }
    return names;
  }

 private:
  std::string fileOf(const std::string& path) const {
    return path.empty() ? root_ : root_ + "/" + path;
  }

  // Whether every component of `path` is an entry of its folder, spelled
  // exactly so.
  bool spelledAsStored(const std::string& path) const {
    std::string parent;
    size_t start = 0;
    while (start < path.size()) {
      size_t end = path.find('/', start);
      if (end == std::string::npos) end = path.size();
      const std::string name = path.substr(start, end - start);
      if (!hasEntry(fileOf(parent), name)) return false;
      parent = parent.empty() ? name : parent + "/" + name;
      start = end + 1;
    }
    return true;
  }

  static bool hasEntry(const std::string& folder, const std::string& name) {
    std::error_code error;
    for (gfs::directory_iterator entry(folder, error), end;
         !error && entry != end; entry.increment(error)) {
      if (entry->path().filename().string() == name) return true;
    }
    return false;
  }

  std::string root_;
};

// Serves `root` as the assets below "apk:/" while it lives.
struct ScopedApkAssets {
  std::shared_ptr<DirectoryBackend> backend;

  explicit ScopedApkAssets(const std::string& root)
      : backend(std::make_shared<DirectoryBackend>(root)) {
    sfz::VirtualFiles::setBackend("apk", backend);
  }
  ~ScopedApkAssets() { sfz::VirtualFiles::setBackend("apk", nullptr); }
  ScopedApkAssets(const ScopedApkAssets&) = delete;
  ScopedApkAssets& operator=(const ScopedApkAssets&) = delete;
};

}  // namespace aud_sampler_test

#endif  // AUD_DIRECTORY_BACKEND_HPP

// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2026, Audanika
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Files sfizz reads besides the filesystem (aud_dsp_sampler, ticket 27 of
// aud_audio_pm): a path "<scheme>:/<path>" names a file a backend serves,
// e.g. "apk:/content/piano.sfz" an asset inside the APK of an Android app.
// The parser, the sample store, the scala tuning and the audio readers ask
// here first; a path without a scheme goes to the filesystem as before.
//
// Written against std::string only, so that the header reads the same in
// every language version: the tests of the node compile as C++17 while the
// fork compiles as gnu++14, where fs::path is a different type.

#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sfz {

/**
 * @brief The bytes of a file a backend serves, mapped or loaded; valid while
 * the object lives.
 */
class VirtualFileData {
public:
    virtual ~VirtualFileData() {}
    virtual const void* data() const noexcept = 0;
    virtual size_t size() const noexcept = 0;
};

/**
 * @brief Serves the files below one scheme, e.g. the assets of an APK below
 * "apk:/". The paths it gets are normalized: relative to its root, separated
 * by '/', without empty, "." or ".." components; the root itself is "".
 *
 * [offline] Called on the threads that load and stream, never on the audio
 * thread; the calls of different threads may overlap.
 */
class VirtualFileBackend {
public:
    virtual ~VirtualFileBackend() {}
    /** The bytes of the file `path`, or -1 when there is no such file. */
    virtual int64_t sizeOf(const std::string& path) = 0;
    /** Opens the file `path`; null when it cannot be read. */
    virtual std::unique_ptr<VirtualFileData> open(const std::string& path) = 0;
    /**
     * The names of the files of `directory`, without its subdirectories and
     * without a path; empty when there is no such directory.
     */
    virtual std::vector<std::string> listFiles(const std::string& directory) = 0;
};

/**
 * @brief The backends by scheme, one registry per process.
 */
class VirtualFiles {
public:
    /**
     * Whether `path` names a virtual file: "<scheme>:/..." or "<scheme>:",
     * with a scheme of at least two letters, so that a Windows drive is none.
     */
    static bool isVirtual(const std::string& path) noexcept;

    /** The scheme of a virtual path, e.g. "apk"; empty for another path. */
    static std::string schemeOf(const std::string& path);

    /**
     * The normalized path inside the backend of a virtual path: "." and ".."
     * resolved, no empty components; "" for the root.
     */
    static std::string pathOf(const std::string& path);

    /** The virtual path in its normalized form, "<scheme>:/<pathOf>". */
    static std::string normalized(const std::string& path);

    /**
     * Registers the backend of `scheme`, replacing the one before; null
     * removes it. [offline] Not while a load that reads the scheme runs.
     */
    static void setBackend(const std::string& scheme, std::shared_ptr<VirtualFileBackend> backend);

    /** The backend of the scheme of `path`; null for a path no backend serves. */
    static std::shared_ptr<VirtualFileBackend> backendOf(const std::string& path);

    /** The bytes of the virtual file, or -1 when no backend has it. */
    static int64_t sizeOf(const std::string& path);

    /** Whether a backend has the virtual file. */
    static bool exists(const std::string& path);

    /** Opens the virtual file; null when no backend can read it. */
    static std::unique_ptr<VirtualFileData> open(const std::string& path);

    /**
     * Whether the virtual path `root / filename` names a file, trying the
     * file name without regard to case when the exact name is not there;
     * `filename` then takes the name the backend has. The directories of
     * the path must match exactly: an APK lists files, not directories.
     */
    static bool checkFile(const std::string& root, std::string& filename) noexcept;

    /** Whether `a` and `b` are the same ASCII letters, case apart. */
    static bool equalsIgnoreCase(const std::string& a, const std::string& b) noexcept;
};

} // namespace sfz

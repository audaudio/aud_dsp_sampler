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

#include "VirtualFile.h"
#include <map>
#include <mutex>

namespace sfz {

namespace {

bool isSchemeChar(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '+' || c == '-' || c == '.';
}

bool isLetter(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// The length of the scheme of `path`, 0 when it has none: a letter, at
// least one more scheme character, a colon, then the end or a slash.
size_t schemeLength(const std::string& path) noexcept
{
    if (path.size() < 3 || !isLetter(path[0]))
        return 0;
    size_t i = 1;
    while (i < path.size() && isSchemeChar(path[i]))
        ++i;
    if (i < 2 || i >= path.size() || path[i] != ':')
        return 0;
    if (i + 1 < path.size() && path[i + 1] != '/')
        return 0;
    return i;
}

// The components of `path` after the scheme, "." and ".." resolved: ".."
// at the root stays at the root.
std::vector<std::string> componentsOf(const std::string& path, size_t scheme)
{
    std::vector<std::string> parts;
    size_t start = scheme + 1;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part == "..") {
            if (!parts.empty())
                parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        start = end + 1;
    }
    return parts;
}

std::string join(const std::vector<std::string>& parts)
{
    std::string result;
    for (const std::string& part : parts) {
        if (!result.empty())
            result += '/';
        result += part;
    }
    return result;
}

char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

struct Registry {
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<VirtualFileBackend>> backends;
};

// Never destroyed: a task may still stream at exit.
Registry& registry()
{
    static Registry* instance = new Registry();
    return *instance;
}

} // namespace

bool VirtualFiles::isVirtual(const std::string& path) noexcept
{
    return schemeLength(path) > 0;
}

std::string VirtualFiles::schemeOf(const std::string& path)
{
    return path.substr(0, schemeLength(path));
}

std::string VirtualFiles::pathOf(const std::string& path)
{
    const size_t scheme = schemeLength(path);
    if (scheme == 0)
        return std::string();
    return join(componentsOf(path, scheme));
}

std::string VirtualFiles::normalized(const std::string& path)
{
    return schemeOf(path) + ":/" + pathOf(path);
}

void VirtualFiles::setBackend(const std::string& scheme, std::shared_ptr<VirtualFileBackend> backend)
{
    Registry& r = registry();
    std::lock_guard<std::mutex> lock { r.mutex };
    if (backend)
        r.backends[scheme] = std::move(backend);
    else
        r.backends.erase(scheme);
}

std::shared_ptr<VirtualFileBackend> VirtualFiles::backendOf(const std::string& path)
{
    const size_t scheme = schemeLength(path);
    if (scheme == 0)
        return nullptr;
    Registry& r = registry();
    std::lock_guard<std::mutex> lock { r.mutex };
    const auto it = r.backends.find(path.substr(0, scheme));
    return it == r.backends.end() ? nullptr : it->second;
}

int64_t VirtualFiles::sizeOf(const std::string& path)
{
    const std::shared_ptr<VirtualFileBackend> backend = backendOf(path);
    return backend ? backend->sizeOf(pathOf(path)) : -1;
}

bool VirtualFiles::exists(const std::string& path)
{
    return sizeOf(path) >= 0;
}

std::unique_ptr<VirtualFileData> VirtualFiles::open(const std::string& path)
{
    const std::shared_ptr<VirtualFileBackend> backend = backendOf(path);
    return backend ? backend->open(pathOf(path)) : nullptr;
}

bool VirtualFiles::checkFile(const std::string& root, std::string& filename) noexcept
{
    try {
        const std::string full = root + "/" + filename;
        const std::shared_ptr<VirtualFileBackend> backend = backendOf(full);
        if (!backend)
            return false;
        const std::string path = pathOf(full);
        if (backend->sizeOf(path) >= 0)
            return true;

        const size_t slash = path.rfind('/');
        const std::string directory = slash == std::string::npos ? std::string() : path.substr(0, slash);
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        for (const std::string& candidate : backend->listFiles(directory)) {
            if (candidate == name || !equalsIgnoreCase(candidate, name))
                continue;
            const size_t nameStart = filename.rfind('/');
            filename = (nameStart == std::string::npos ? std::string() : filename.substr(0, nameStart + 1)) + candidate;
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool VirtualFiles::equalsIgnoreCase(const std::string& a, const std::string& b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

} // namespace sfz

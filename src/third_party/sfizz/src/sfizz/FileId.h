// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 30 of aud_audio_pm): an
// identifier computes its hash once, which a voice that starts looks its
// file up by, instead of hashing the file name at every lookup.

#pragma once
#include <cstddef>
#include <string>
#include <memory>
#include <iosfwd>

namespace sfz {

/**
 * @brief Sample file identifier within a file pool.
 */
struct FileId {
    /**
     * @brief Construct a null identifier.
     */
    FileId()
        : hash_(hashOf(std::string(), false))
    {
    }

    /**
     * @brief Construct a file identifier, optionally reversed.
     *
     * @param filename
     * @param reverse
     */
    explicit FileId(std::string filename, bool reverse = false)
        : filenameBuffer(new std::string(std::move(filename))),
          reverse(reverse),
          hash_(hashOf(*filenameBuffer, reverse))
    {
    }

    /**
     * @brief Make an identifier which is a clone of the callee, except with the
     * reverse flag passed as parameter.
     *
     * @param reverse
     */
    FileId reversed(bool reverse = true)
    {
        FileId id;
        id.filenameBuffer = filenameBuffer;
        id.reverse = reverse;
        id.hash_ = hashOf(filename(), reverse);
        return id;
    }

    /**
     * @brief Get the file name of this identifier.
     *
     * @return file name
     */
    const std::string &filename() const noexcept
    {
        return filenameBuffer ? *filenameBuffer : emptyFilename;
    }

    /**
     * @brief Get whether the identified file is reversed.
     *
     * @return bool
     */
    bool isReverse() const noexcept
    {
        return reverse;
    }

    /**
     * @brief Check equality with another identifier.
     *
     * @param other
     */
    bool operator==(const FileId &other) const
    {
        return reverse == other.reverse && filename() == other.filename();
    }

    /**
     * @brief Check inequality with another identifier.
     *
     * @param other
     */
    bool operator!=(const FileId &other) const
    {
        return !operator==(other);
    }

    /**
     * @brief Get the hash of this identifier.
     */
    size_t hash() const noexcept
    {
        return hash_;
    }

private:
    static size_t hashOf(const std::string& filename, bool reverse) noexcept;

    std::shared_ptr<std::string> filenameBuffer;
    bool reverse = false;
    size_t hash_ = 0;
    static const std::string emptyFilename;
};

}

namespace std {
    template <> struct hash<sfz::FileId> {
        size_t operator()(const sfz::FileId &id) const;
    };
}

std::ostream &operator<<(std::ostream &os, const sfz::FileId &fileId);

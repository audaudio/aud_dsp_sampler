// SPDX-License-Identifier: BSD-2-Clause

// This code is part of the sfizz library and is licensed under a BSD 2-clause
// license. You should have receive a LICENSE.md file along with the code.
// If not, contact the sfizz maintainers at https://github.com/sfztools/sfizz

// Modified by Audanika for aud_dsp_sampler (ticket 30 of aud_audio_pm): the
// hash of an identifier is computed once, at its construction.

#include "FileId.h"
#include "utility/StringViewHelpers.h"
#include <iostream>

size_t sfz::FileId::hashOf(const std::string& filename, bool reverse) noexcept
{
    uint64_t h = ::hash(filename);
    h = ::hash(reverse ? "!" : "", h);
    return h;
}

size_t std::hash<sfz::FileId>::operator()(const sfz::FileId &id) const
{
    return id.hash();
}

std::ostream &operator<<(std::ostream &os, const sfz::FileId &fileId)
{
    os << fileId.filename();
    if (fileId.isReverse())
        os << " (reverse)";
    return os;
}

const std::string sfz::FileId::emptyFilename;

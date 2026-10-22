// Copyright (c) 2013 Steinwurf ApS
// All Rights Reserved
//
// Distributed under the "BSD License". See the accompanying LICENSE.rst file.
//
// The MSVC variant of init_gcc_x86.hpp, written for aud_dsp_sampler on
// Windows (ticket 38): the same calls through the __cpuidex intrinsic.

#pragma once

#include <cstdint>

#include <intrin.h>

#include "cpuinfo_impl.hpp"
#include "extract_x86_flags.hpp"

namespace cpuid
{
inline namespace STEINWURF_CPUID_VERSION
{
void run_cpuid(uint32_t eax, uint32_t ecx, uint32_t* abcd)
{
    int registers[4];
    __cpuidex(registers, static_cast<int>(eax), static_cast<int>(ecx));
    abcd[0] = static_cast<uint32_t>(registers[0]);
    abcd[1] = static_cast<uint32_t>(registers[1]);
    abcd[2] = static_cast<uint32_t>(registers[2]);
    abcd[3] = static_cast<uint32_t>(registers[3]);
}

void init_cpuinfo(cpuinfo::impl& info)
{
    uint32_t output[4];

    // CPUID with EAX=0 returns the highest index the CPU supports.
    run_cpuid(0, 0, output);
    uint32_t maximum_index = output[0];

    if (maximum_index >= 1U)
    {
        run_cpuid(1, 0, output);
        extract_x86_flags(info, output[2], output[3]);
    }

    if (maximum_index >= 7U)
    {
        run_cpuid(7, 0, output);
        extract_x86_extended_flags(info, output[1], output[2], output[3]);
    }
}
}
}

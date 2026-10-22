// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

// Renders an instrument like an app's (sfizz_app_instrument.hpp) and
// compares it with the golden the code of release 0.3.0 rendered, before
// ticket 28 made the MIDI state, the compressor, the gain, the effect bus
// and the conversions of the voice faster: the render is the same within
// 1e-6. `UPDATE_GOLDENS=1 node scripts/test-native.js --bench` writes the
// golden with the code of that release.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <chrono>
#include <system_error>

#include "ghc/filesystem.hpp"

#include "sfizz_app_instrument.hpp"

#include "aud_test.hpp"

namespace {

// A temporary folder, removed at the end; ghc's filesystem, so that it
// builds on every host, Windows among them (ticket 38).
struct Folder {
    std::string path;

    Folder()
    {
        const auto run = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto folder = ghc::filesystem::temp_directory_path()
            / ("sfizz_app_" + std::to_string(run));
        std::error_code error;
        if (ghc::filesystem::create_directories(folder, error))
            path = folder.generic_string();
    }
    ~Folder()
    {
        std::error_code ignored;
        if (!path.empty())
            ghc::filesystem::remove_all(path, ignored);
    }
};

const char* const kGolden = "test/goldens/sfizz_app_instrument.wav";

} // namespace

AUD_TEST(sfizz_app_instrument_renders_like_the_original) {
    Folder folder;
    AUD_CHECK(!folder.path.empty());
    const std::string text = sfizz_app::writeInstrument(folder.path);
    AUD_CHECK(!text.empty());
    const std::vector<float> render = sfizz_app::render(folder.path, text, false, sfizz_app::kGoldenBlocks);
    const std::vector<float> golden = sfizz_app::readWav(kGolden);
    AUD_CHECK(render.size() == static_cast<size_t>(sfizz_app::kGoldenBlocks) * sfizz_app::kBlockFrames * 2);
    AUD_CHECK(golden.size() == render.size());
    if (golden.size() != render.size())
        return;
    float largest = 0;
    float peak = 0;
    size_t differingBits = 0;
    for (size_t i = 0; i < render.size(); ++i) {
        largest = std::max(largest, std::fabs(render[i] - golden[i]));
        peak = std::max(peak, std::fabs(golden[i]));
        differingBits += std::memcmp(&render[i], &golden[i], sizeof(float)) == 0 ? 0 : 1;
    }
    if (largest > 1e-6f || differingBits != 0)
        std::printf("  the render differs by %g, %zu samples in their bits\n", largest, differingBits);
#if _WIN32
    // The golden comes from arm64; x86-64 and the C runtime of Windows
    // render the last bits differently, 4.2e-6 at most (ticket 38).
    AUD_CHECK(largest <= 1e-5f);
#else
    AUD_CHECK(largest <= 1e-6f);
#endif
    // The chord drives the limiter: the golden holds a loud part; the first
    // blocks are silent, the bus skips its effects there.
    AUD_CHECK(peak > 0.5f);
    AUD_CHECK(std::all_of(golden.begin(), golden.begin() + 10 * sfizz_app::kBlockFrames * 2,
        [](float sample) { return sample == 0.0f; }));
}

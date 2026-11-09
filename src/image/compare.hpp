/// @file compare.hpp
/// @brief Core linear image and AOV quality checks from BENCHMARKING.md.
#pragma once

#include <cstdint>
#include <string>

#include "image/image.hpp"

namespace rt {

/// Starting quality tolerances; changes require real CPU/GPU calibration data.
struct CompareSettings {
    double absolute = 1.0e-5;
    double relative = 1.0e-4;
    double mismatch_fraction = 0.0005;  ///< 0.05 percent of all pixels.
};

/// Counts are per pixel, not per RGB channel. The broad sRGB summary is informational.
struct CompareResult {
    uint64_t pixels = 0;
    uint64_t nonfinite = 0;
    uint64_t unexplained_hits = 0;
    uint64_t unexplained_shadows = 0;
    uint64_t explained_mismatches = 0;
    uint64_t shading_mismatches = 0;
    uint64_t srgb_close = 0;
    bool budget_pass = true;
    /// True iff all five required checks pass.
    bool passed() const {
        return nonfinite == 0 && unexplained_hits == 0 && unexplained_shadows == 0 && budget_pass &&
               shading_mismatches == 0;
    }
};

/// Reads a linear PFM and both AOV sidecars, checking all three resolutions agree.
/// @throws Error if files are invalid, missing, or inconsistent.
Image read_comparison_image(const std::string& pfm_path);

/// Compares finite linear samples and IDs/masks with the bounded 3x3 mismatch allowance.
/// Requires RGB and both AOV arrays at matching dimensions in both images.
/// @throws Error on inconsistent input sizes or invalid tolerance settings.
CompareResult compare_images(const Image& reference, const Image& test,
                             const CompareSettings& settings = {});

}  // namespace rt

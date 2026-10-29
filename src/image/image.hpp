/// @file image.hpp
/// @brief Host-owned linear RGB image and optional per-pixel auxiliary outputs.
#pragma once

#include <cstdint>
#include <vector>

namespace rt {

/// Caller-owned output. All arrays are row-major with row zero at the top of the image.
struct Image {
    uint32_t width = 0, height = 0;    ///< Image dimensions.
    std::vector<float> rgb;            ///< Linear RGB, three floats per pixel, before clamping.
    std::vector<uint32_t> prim_id;     ///< Optional accepted primary-hit IDs.
    std::vector<uint32_t> light_mask;  ///< Optional contributing-light bit masks.
};

}  // namespace rt

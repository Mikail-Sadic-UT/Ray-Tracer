/// @file normal_renderer.hpp
/// @brief CPU normal-shaded reference rendering independent of the backend lifecycle.
#pragma once

#include "bench/timings.hpp"
#include "image/image.hpp"
#include "scene/scene_host.hpp"

namespace rt {

/// Explicit settings for the first CPU image. Normal shading casts no shadow rays.
struct NormalRenderSettings {
    int threads = 1;           ///< OpenMP render thread count; must be positive.
    bool prim_ids = false;     ///< Write accepted primary-hit IDs.
    bool light_masks = false;  ///< Write masks (all zero for NormalShading).
};

/// Renders with BruteQuery and NormalShading over dynamic 16x16 OpenMP tiles.
/// Resizes output, clears unrequested AOVs, and records the pixel loop in timings.render.
/// Output is bit-identical for every thread count. Scene and output must be distinct storage.
/// @throws Error for invalid scene input or nonpositive thread count.
void render_normal(const SceneHost& scene, Image& output, const NormalRenderSettings& settings,
                   Timings& timings);

}  // namespace rt

/// @file generator.hpp
/// @brief Repeatable uniform scene generation and fitted-camera diagnostics.
#pragma once

#include "core/aabb.hpp"
#include "scene/scene_host.hpp"

namespace rt {

/// Explicit uniform-generator inputs; the terrain shape is independent of the seed.
struct UniformSettings {
    uint32_t primitives = 10000;
    uint64_t seed = 1;
};

/// Computes bounds including sphere radii and every triangle vertex.
/// @pre Scene geometry is nonempty and valid.
AABB scene_bounds(const SceneHost& scene);

/// Fits a 45-degree, 1920x1080 camera at 35-degree elevation to 95% of both half-frame extents.
/// Uses deterministic bisection and includes every AABB corner.
Camera fit_camera(const AABB& bounds);

/// Builds uniform spheres plus a height-field grid, exactly N primitives, and validates/hashes it.
/// Requires 4 <= N < 2^31. Same binary and inputs produce identical scene data.
SceneHost generate_uniform(const UniformSettings& settings);

}  // namespace rt

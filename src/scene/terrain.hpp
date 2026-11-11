/// @file terrain.hpp
/// @brief Deterministic benchmark height field and upward-wound triangle grid.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/error.hpp"
#include "scene/scene.hpp"

namespace rt {

/// Evaluates the seed-independent benchmark height field, bounded by [-2.5, 2.5].
/// @pre Coordinates are finite and their trigonometric arguments do not overflow.
inline float terrain_height(float x, float z) {
    constexpr float pi = 3.14159265358979323846f;
    return 2.0f * std::sin(2.0f * pi * x / 40.0f) * std::cos(2.0f * pi * z / 40.0f) +
           0.5f * std::sin(2.0f * pi * x / 9.0f + 1.0f);
}

/// Builds 2*g*g triangles over a centered square of side @p side, in z-major cell order.
/// Each cell uses the lower-left to upper-right diagonal and upward winding.
/// Materials are (spheres + triangle index) % 8, following canonical primitive order.
/// Throws Error for zero grid size, invalid extent, or a total count outside [1, 2^31).
inline std::vector<Triangle> terrain_grid(uint32_t g, float side, uint32_t spheres) {
    if (g == 0 || !std::isfinite(side) || side <= 0.0f)
        throw Error("terrain: grid must be nonzero and side must be finite and positive");
    const uint64_t cells = uint64_t{g} * g;
    if (cells >= (uint64_t{1} << 30) || 2 * cells + spheres >= (uint64_t{1} << 31))
        throw Error("terrain: total primitive count must be less than 2^31");
    std::vector<Triangle> triangles;
    if (2 * cells > triangles.max_size()) throw Error("terrain: grid exceeds vector capacity");
    triangles.reserve(static_cast<size_t>(cells * 2));
    const auto point = [&](uint32_t i, uint32_t j) {
        const float x = -0.5f * side + side * static_cast<float>(i) / static_cast<float>(g);
        const float z = -0.5f * side + side * static_cast<float>(j) / static_cast<float>(g);
        return vec3{x, terrain_height(x, z), z};
    };
    for (uint32_t z = 0; z < g; ++z) {
        for (uint32_t x = 0; x < g; ++x) {
            const auto a = point(x, z), b = point(x + 1, z);
            const auto c = point(x, z + 1), d = point(x + 1, z + 1);
            triangles.push_back(
                {a, c, d, (spheres + static_cast<uint32_t>(triangles.size())) % 8u});
            triangles.push_back(
                {a, d, b, (spheres + static_cast<uint32_t>(triangles.size())) % 8u});
        }
    }
    return triangles;
}

}  // namespace rt

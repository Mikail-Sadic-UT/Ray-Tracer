/// @file terrain_stub.hpp
/// @brief Local terrain stand-in for generator development pending Jett's hand-off.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "scene/scene.hpp"

namespace rt::detail {

// STUB(S3, po): remove when Jett's terrain height-field generator lands (#35).
/// Matches the documented height formula for testing the generator before terrain integration.
inline float terrain_height_stub(float x, float z) {
    constexpr float pi = 3.14159265358979323846f;
    return 2.0f * std::sin(2.0f * pi * x / 40.0f) * std::cos(2.0f * pi * z / 40.0f) +
           0.5f * std::sin(2.0f * pi * x / 9.0f + 1.0f);
}

/// Builds an upward-wound g by g grid with material IDs following canonical primitive order.
inline std::vector<Triangle> terrain_grid_stub(uint32_t g, float side, uint32_t spheres) {
    std::vector<Triangle> triangles;
    triangles.reserve(static_cast<size_t>(g) * g * 2);
    const auto point = [&](uint32_t i, uint32_t j) {
        const float x = -0.5f * side + side * static_cast<float>(i) / static_cast<float>(g);
        const float z = -0.5f * side + side * static_cast<float>(j) / static_cast<float>(g);
        return vec3{x, terrain_height_stub(x, z), z};
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

}  // namespace rt::detail

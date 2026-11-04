/// @file hit.hpp
/// @brief Closest-hit record returned by tracing queries.
#pragma once

#include <cstdint>
#include <type_traits>

#include "bvh/bvh_types.hpp"

namespace rt {

/// Primitive reference stored by a miss.
constexpr PrimRef RT_MISS_PRIM = 0xFFFFFFFFu;

/// Closest hit along a ray (16 bytes, POD).
struct Hit {
    float t;       ///< Distance in units of `ray.dir`; `ray.tmax` on a miss.
    PrimRef prim;  ///< Sphere index, or `PRIM_TRIANGLE_BIT | triangle index`.
    float u;       ///< Barycentric weight of `Triangle::v1`; 0 for spheres.
    float v;       ///< Barycentric weight of `Triangle::v2`; 0 for spheres.
};

static_assert(sizeof(Hit) == 16, "Hit must be 16 bytes");
static_assert(std::is_standard_layout<Hit>::value && std::is_trivially_copyable<Hit>::value,
              "Hit must have a standard layout and be trivially copyable");

}  // namespace rt

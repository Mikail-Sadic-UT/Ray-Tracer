/// @file ray.hpp
/// @brief Ray type shared by every backend.
#pragma once

#include "core/math.hpp"

namespace rt {

/// Ray `origin + t * dir`, valid for `tmin < t < tmax` (32 bytes, POD).
///
/// Each vec3 is followed by a float so the struct packs into two 16-byte halves.
struct Ray {
    vec3 origin;  ///< Ray origin.
    float tmin;   ///< Lower bound of the valid t interval (exclusive).
    vec3 dir;     ///< Direction. Not required to be normalized.
    float tmax;   ///< Upper bound of the valid t interval (exclusive).
};

static_assert(sizeof(Ray) == 32, "Ray must be 32 bytes");

}  // namespace rt

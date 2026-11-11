/// @file brute.hpp
/// @brief Brute-force closest-hit and any-hit loops and the BruteQuery tracing query.
///
/// These run the same per-primitive step as BVH traversal (trace/accept.hpp), so the two return
/// identical hits. They serve as the test oracle and as the `--bvh none` query.
#pragma once

#include <cstdint>

#include "core/ray.hpp"
#include "scene/scene.hpp"
#include "trace/accept.hpp"
#include "trace/hit.hpp"

namespace rt {

/// Closest hit over every sphere and triangle; test oracle for BVH traversal.
/// @return The accepted hit, or `{r.tmax, RT_MISS_PRIM, 0, 0}` on a miss. Equal distances
///         resolve to the lowest PrimRef.
/// @pre `s.num_spheres < 2^31` and `s.num_triangles < 2^31`, so every PrimRef differs
///      from `RT_MISS_PRIM`. Null arrays are allowed when their count is 0.
RT_INLINE Hit brute_closest(const Ray& r, const Scene& s) {
    Hit best{r.tmax, RT_MISS_PRIM, 0.0f, 0.0f};
    bool found = false;
    for (uint32_t i = 0; i < s.num_spheres; ++i) detail::closest_prim(r, s, i, best, found);
    for (uint32_t i = 0; i < s.num_triangles; ++i) {
        detail::closest_prim(r, s, PRIM_TRIANGLE_BIT | i, best, found);
    }
    return best;
}

/// True iff any sphere or triangle has a hit with `r.tmin < t < r.tmax` (shadow rays).
/// @pre Same as brute_closest.
RT_INLINE bool brute_any(const Ray& r, const Scene& s) {
    for (uint32_t i = 0; i < s.num_spheres; ++i) {
        if (detail::any_prim(r, s, i)) return true;
    }
    for (uint32_t i = 0; i < s.num_triangles; ++i) {
        if (detail::any_prim(r, s, PRIM_TRIANGLE_BIT | i)) return true;
    }
    return false;
}

/// Tracing query that tests every primitive (`--bvh none`, first images, tests).
struct BruteQuery {
    Scene s;  ///< Scene view; its arrays live in host or device memory to match the caller.

    /// Closest hit for a primary ray.
    RT_INLINE Hit closest(const Ray& r) const { return brute_closest(r, s); }

    /// Occlusion test for a shadow ray over `(r.tmin, r.tmax)`.
    RT_INLINE bool any(const Ray& r) const { return brute_any(r, s); }
};

}  // namespace rt

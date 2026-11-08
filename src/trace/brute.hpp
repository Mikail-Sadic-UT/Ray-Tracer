/// @file brute.hpp
/// @brief Brute-force closest-hit and any-hit loops and the BruteQuery tracing query.
///
/// These follow the same acceptance rule as BVH traversal (INTERFACES §5), so the two return
/// identical hits. They serve as the test oracle and as the `--bvh none` query.
#pragma once

#include <cstdint>

#include "core/ray.hpp"
#include "scene/scene.hpp"
#include "trace/hit.hpp"
#include "trace/intersect.hpp"

namespace rt {
namespace detail {

/// Closest-hit acceptance rule (INTERFACES §5): candidate `(t, p)` replaces @p best iff
/// `r.tmin < t < r.tmax` and (no hit yet, or `t < best.t`, or `t == best.t && p < best.prim`).
RT_INLINE bool accept(const Ray& r, bool found, const Hit& best, float t, PrimRef p) {
    if (!(t > r.tmin && t < r.tmax)) return false;
    return !found || t < best.t || (t == best.t && p < best.prim);
}

}  // namespace detail

/// Closest hit over every sphere and triangle; test oracle for BVH traversal.
/// @return The accepted hit, or `{r.tmax, RT_MISS_PRIM, 0, 0}` on a miss. Equal distances
///         resolve to the lowest PrimRef.
/// @pre `s.num_spheres < 2^31` and `s.num_triangles < 2^31`, so every PrimRef differs
///      from `RT_MISS_PRIM`. Null arrays are allowed when their count is 0.
RT_INLINE Hit brute_closest(const Ray& r, const Scene& s) {
    Hit best{r.tmax, RT_MISS_PRIM, 0.0f, 0.0f};
    bool found = false;
    for (uint32_t i = 0; i < s.num_spheres; ++i) {
        float t;
        if (intersect_sphere(r, s.spheres[i], best.t, t) && detail::accept(r, found, best, t, i)) {
            best = Hit{t, i, 0.0f, 0.0f};
            found = true;
        }
    }
    for (uint32_t i = 0; i < s.num_triangles; ++i) {
        float t, u, v;
        const PrimRef p = PRIM_TRIANGLE_BIT | i;
        if (intersect_triangle(r, s.triangles[i], best.t, t, u, v) &&
            detail::accept(r, found, best, t, p)) {
            best = Hit{t, p, u, v};
            found = true;
        }
    }
    return best;
}

/// True iff any sphere or triangle has a hit with `r.tmin < t < r.tmax` (shadow rays).
/// @pre Same as brute_closest.
RT_INLINE bool brute_any(const Ray& r, const Scene& s) {
    for (uint32_t i = 0; i < s.num_spheres; ++i) {
        float t;
        if (intersect_sphere(r, s.spheres[i], r.tmax, t) && t < r.tmax) return true;
    }
    for (uint32_t i = 0; i < s.num_triangles; ++i) {
        float t, u, v;
        if (intersect_triangle(r, s.triangles[i], r.tmax, t, u, v) && t < r.tmax) return true;
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

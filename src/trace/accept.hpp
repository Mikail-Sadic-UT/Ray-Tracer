/// @file accept.hpp
/// @brief Per-primitive closest-hit and any-hit steps shared by brute force and BVH traversal.
///
/// `brute_closest` and `traverse_closest` must return the same Hit (INTERFACES §5). Both call
/// detail::closest_prim() for every primitive they test, so they agree by construction.
#pragma once

#include <cstdint>

#include "bvh/bvh_types.hpp"
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

/// Tests primitive @p p against @p r and replaces @p best if the hit is accepted.
///
/// Passes `t_hi = best.t`, which is `r.tmax` until the first hit, so a candidate at exactly
/// `best.t` reaches the tie rule and one at `r.tmax` is rejected by accept().
/// @param[in,out] best  Closest hit so far; starts as `{r.tmax, RT_MISS_PRIM, 0, 0}`.
/// @param[in,out] found Whether @p best holds a hit.
/// @pre @p p indexes an existing sphere or triangle of @p s.
RT_INLINE void closest_prim(const Ray& r, const Scene& s, PrimRef p, Hit& best, bool& found) {
    if (p & PRIM_TRIANGLE_BIT) {
        float t, u, v;
        if (intersect_triangle(r, s.triangles[p & ~PRIM_TRIANGLE_BIT], best.t, t, u, v) &&
            accept(r, found, best, t, p)) {
            best = Hit{t, p, u, v};
            found = true;
        }
    } else {
        float t;
        if (intersect_sphere(r, s.spheres[p], best.t, t) && accept(r, found, best, t, p)) {
            best = Hit{t, p, 0.0f, 0.0f};
            found = true;
        }
    }
}

/// True iff primitive @p p has a hit with `r.tmin < t < r.tmax` (shadow rays).
/// @pre @p p indexes an existing sphere or triangle of @p s.
RT_INLINE bool any_prim(const Ray& r, const Scene& s, PrimRef p) {
    float t;
    if (p & PRIM_TRIANGLE_BIT) {
        float u, v;
        return intersect_triangle(r, s.triangles[p & ~PRIM_TRIANGLE_BIT], r.tmax, t, u, v) &&
               t < r.tmax;
    }
    return intersect_sphere(r, s.spheres[p], r.tmax, t) && t < r.tmax;
}

}  // namespace detail
}  // namespace rt

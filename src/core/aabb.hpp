/// @file aabb.hpp
/// @brief Axis-aligned bounding box and the ray-box slab test.
#pragma once

#include <math.h>

#include <cfloat>

#include "core/ray.hpp"

namespace rt {

/// Unit roundoff for float, 2^-24.
constexpr float RT_EPS = 5.9604645e-8f;

/// gamma(3) = 3u / (1 - 3u): relative error bound after three float roundings (PBRT).
constexpr float RT_GAMMA3 = (3.0f * RT_EPS) / (1.0f - 3.0f * RT_EPS);

/// Closed axis-aligned box `[lo, hi]` (24 bytes, POD).
///
/// The empty box is `lo = {+inf, +inf, +inf}`, `hi = {-inf, -inf, -inf}`; growing it by a point
/// or box gives exactly that point or box.
struct AABB {
    vec3 lo;  ///< Minimum corner.
    vec3 hi;  ///< Maximum corner.

    /// Expands the box to contain point @p p.
    RT_INLINE void grow(vec3 p) {
        lo = vmin(lo, p);
        hi = vmax(hi, p);
    }

    /// Expands the box to contain box @p b.
    RT_INLINE void grow(const AABB& b) {
        lo = vmin(lo, b.lo);
        hi = vmax(hi, b.hi);
    }

    /// Center point of the box.
    RT_INLINE vec3 centroid() const { return (lo + hi) * 0.5f; }

    /// Size along each axis (`hi - lo`).
    RT_INLINE vec3 extent() const { return hi - lo; }

    /// Total surface area.
    /// @pre The box is not empty.
    RT_INLINE float surface_area() const {
        const vec3 e = extent();
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }

    /// True if `lo > hi` on any axis. A flat box (`lo == hi` on an axis) is not empty.
    RT_INLINE bool is_empty() const { return lo.x > hi.x || lo.y > hi.y || lo.z > hi.z; }
};

/// Per-ray values for the slab test, computed once per ray.
struct RayPrecomp {
    vec3 inv_d;  ///< `1 / dir` per component. A +0 component gives +inf, -0 gives -inf.
    vec3 dir;    ///< Original direction, used if the reciprocal overflowed.
    int reciprocal_overflow_mask;  ///< Bit a is set when 1 / dir[a] overflowed for nonzero dir[a].
    int neg[3];  ///< `neg[a]` is 1 if `inv_d[a] < 0`. Selects the near and far planes per axis.
};

/// Computes the slab-test values for ray @p r.
///
/// The sign is taken from `inv_d` rather than `dir`, so a -0 direction component counts as
/// negative.
RT_INLINE RayPrecomp precompute(const Ray& r) {
    RayPrecomp p;
    p.inv_d = {1.0f / r.dir.x, 1.0f / r.dir.y, 1.0f / r.dir.z};
    p.dir = r.dir;
    p.reciprocal_overflow_mask = 0;
    for (int a = 0; a < 3; ++a) {
        p.neg[a] = p.inv_d[a] < 0.0f;
        if (r.dir[a] != 0.0f && (p.inv_d[a] > FLT_MAX || p.inv_d[a] < -FLT_MAX)) {
            p.reciprocal_overflow_mask |= 1 << a;
        }
    }
    return p;
}

namespace detail {

/// Computes float axis distances once, returning false if an intermediate could overflow.
RT_INLINE bool slab_axis_float_distances(const AABB& b, const vec3& o, const RayPrecomp& p, int a,
                                         float& tn, float& tf) {
    const float near_plane = p.neg[a] ? b.hi[a] : b.lo[a];
    const float far_plane = p.neg[a] ? b.lo[a] : b.hi[a];
    const float near_delta = near_plane - o[a];
    const float far_delta = far_plane - o[a];
    tn = near_delta * p.inv_d[a];
    tf = far_delta * p.inv_d[a];
    // For finite nonzero directions, inv_d is nonzero under gradual underflow. An overflowing
    // subtraction therefore also produces a non-finite product. Parallel axes retain their
    // intentional infinity/NaN behavior.
    if (p.dir[a] != 0.0f && (!::isfinite(tn) || !::isfinite(tf))) {
        return false;
    }
    return true;
}

/// Complete double-precision interval calculation used when any float axis is unsafe.
RT_INLINE bool intersect_aabb_double(const AABB& b, const vec3& o, const RayPrecomp& p, float t_lo,
                                     float t_hi, float& t_enter) {
    double t0 = static_cast<double>(t_lo);
    double t1 = static_cast<double>(t_hi);
    for (int a = 0; a < 3; ++a) {
        const double origin = static_cast<double>(o[a]);
        const double direction = static_cast<double>(p.dir[a]);
        const double lo = static_cast<double>(b.lo[a]);
        const double hi = static_cast<double>(b.hi[a]);
        if (direction == 0.0) {
            if (origin < lo || origin > hi) {
                return false;
            }
            continue;
        }
        const double near_plane = direction < 0.0 ? hi : lo;
        const double far_plane = direction < 0.0 ? lo : hi;
        const double tn = (near_plane - origin) / direction;
        double tf = (far_plane - origin) / direction;
        tf *= 1.0 + 2.0 * static_cast<double>(RT_GAMMA3);
        t0 = tn > t0 ? tn : t0;
        t1 = tf < t1 ? tf : t1;
        if (t0 > t1) {
            return false;
        }
    }
    t_enter = static_cast<float>(t0);
    return true;
}

}  // namespace detail

/// Tests whether a ray hits box @p b within the closed range `[t_lo, t_hi]`.
///
/// For each axis, the near and far planes are chosen from the ray's direction sign, then the
/// entry and exit distances are intersected with the running interval. The far distance is
/// widened slightly so rounding error never causes a false miss (PBRT v4).
///
/// A zero direction component with the origin on a slab plane produces NaN (0 * inf). The
/// interval updates ignore NaN, so that axis does not constrain the ray. If any axis has an
/// overflowing reciprocal, coordinate difference, or distance product, the complete box
/// interval is recomputed in double. The fallback handles parallel axes explicitly, including
/// both signs of zero, and applies the same conservative far-bound widening.
///
/// @param b        Box to test.
/// @param o        Ray origin.
/// @param p        Values from precompute() for the same ray.
/// @param t_lo     Start of the t range (inclusive).
/// @param t_hi     End of the t range (inclusive).
/// @param t_enter  On a hit, the entry distance clamped to @p t_lo. Unchanged on a miss.
/// @return True if the ray overlaps the box anywhere in `[t_lo, t_hi]`.
RT_INLINE bool intersect_aabb(const AABB& b, const vec3& o, const RayPrecomp& p, float t_lo,
                              float t_hi, float& t_enter) {
    // Reciprocal overflow is ray-wide metadata; check it once instead of once per axis.
    if (p.reciprocal_overflow_mask != 0) {
        return detail::intersect_aabb_double(b, o, p, t_lo, t_hi, t_enter);
    }

    float t0 = t_lo;
    float t1 = t_hi;
    for (int a = 0; a < 3; ++a) {
        float tn;
        float tf;
        if (!detail::slab_axis_float_distances(b, o, p, a, tn, tf)) {
            return detail::intersect_aabb_double(b, o, p, t_lo, t_hi, t_enter);
        }
        tf *= 1.0f + 2.0f * RT_GAMMA3;  // conservative far bound
        // NaN compares false, so a NaN tn or tf leaves t0 or t1 unchanged.
        t0 = tn > t0 ? tn : t0;
        t1 = tf < t1 ? tf : t1;
        // Strict compare: a single touching point (t0 == t1) is a hit.
        if (t0 > t1) {
            // The safe axes already have an empty interval; additional axes can only narrow it.
            return false;
        }
    }
    t_enter = t0;
    return true;
}

static_assert(sizeof(AABB) == 24, "AABB must be 24 bytes");

}  // namespace rt

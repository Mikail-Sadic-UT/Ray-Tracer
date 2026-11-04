/// @file intersect.hpp
/// @brief Ray-sphere (stable quadratic) and ray-triangle (watertight) intersection tests.
///
/// Both tests report a hit iff a root lies in `(r.tmin, t_hi]` (INTERFACES §5). The inclusive
/// upper bound lets a caller pass `t_hi = best.t` so equal distances reach the tie rule.
#pragma once

#include <cmath>

#include "core/math.hpp"
#include "core/ray.hpp"
#include "scene/scene.hpp"

namespace rt {
namespace detail {

/// `a*b - c*d` with Kahan's FMA algorithm: relative error at most 1.5 ulp, and the sign of the
/// exact result.
RT_INLINE float difference_of_products(float a, float b, float c, float d) {
    const float cd = c * d;
    const float dop = ::fmaf(a, b, -cd);
    const float err = ::fmaf(-c, d, cd);
    return dop + err;
}

/// Index of the largest component. Ties go to the later axis, as in PBRT.
RT_INLINE int max_component_index(vec3 a) {
    return a.x > a.y ? (a.x > a.z ? 0 : 2) : (a.y > a.z ? 1 : 2);
}

/// Returns `{a[x], a[y], a[z]}`.
RT_INLINE vec3 permute(vec3 a, int x, int y, int z) {
    return {a[x], a[y], a[z]};
}

/// Edge functions of a triangle already translated, permuted, and sheared into ray space.
///
/// If any float result is exactly 0, all three are recomputed in double from the same float
/// inputs (PBRT v4 Sec. 6.5.3). This is the only double arithmetic in render code.
/// @return true if the double fallback ran.
RT_INLINE bool edge_functions(vec3 p0, vec3 p1, vec3 p2, float& e0, float& e1, float& e2) {
    e0 = difference_of_products(p1.x, p2.y, p1.y, p2.x);
    e1 = difference_of_products(p2.x, p0.y, p2.y, p0.x);
    e2 = difference_of_products(p0.x, p1.y, p0.y, p1.x);
    if (e0 != 0.0f && e1 != 0.0f && e2 != 0.0f) return false;
    // A product of two floats is exact in double, so each difference rounds only once.
    e0 = static_cast<float>(double(p1.x) * double(p2.y) - double(p1.y) * double(p2.x));
    e1 = static_cast<float>(double(p2.x) * double(p0.y) - double(p2.y) * double(p0.x));
    e2 = static_cast<float>(double(p0.x) * double(p1.y) - double(p0.y) * double(p1.x));
    return true;
}

/// True if the float cross product of the triangle's edges is exactly zero (zero area).
RT_INLINE bool zero_area(const Triangle& tri) {
    const vec3 a = tri.v1 - tri.v0;
    const vec3 b = tri.v2 - tri.v0;
    return difference_of_products(a.y, b.z, a.z, b.y) == 0.0f &&
           difference_of_products(a.z, b.x, a.x, b.z) == 0.0f &&
           difference_of_products(a.x, b.y, a.y, b.x) == 0.0f;
}

}  // namespace detail

/// Ray-sphere test with a stable quadratic and PBRT v4's accurate discriminant (Sec. 6.8.3).
/// @param t_hi Inclusive upper bound; roots with `r.tmin < t <= t_hi` count.
/// @param[out] t Smallest root in that interval; unchanged on a miss.
/// @return true if a root lies in `(r.tmin, t_hi]`. A ray starting inside the sphere with
///         `r.tmin = 0` gets the far root.
/// @pre `r.dir` is nonzero and finite; `s.radius > 0`. Non-finite inputs give a miss.
RT_INLINE bool intersect_sphere(const Ray& r, const Sphere& s, float t_hi, float& t) {
    const vec3 oc = r.origin - s.center;
    const float a = dot(r.dir, r.dir);
    const float b = 2.0f * dot(oc, r.dir);
    const float c = dot(oc, oc) - s.radius * s.radius;
    // f is the offset from the center to the ray's closest approach. Computing the
    // discriminant from it avoids the cancellation in b*b - 4ac for distant spheres.
    const vec3 f = oc - (b / (2.0f * a)) * r.dir;
    const float len_f = length(f);
    const float disc = 4.0f * a * (s.radius - len_f) * (s.radius + len_f);
    if (!(disc >= 0.0f)) return false;  // also rejects NaN
    const float q = -0.5f * (b + ::copysignf(::sqrtf(disc), b));
    float t0 = q / a;
    // q == 0 only for a ray that touches the sphere at its origin (b == 0, disc == 0): a double
    // root at t0 == +-0. c need not round to 0 there, so c / q would be +-inf or NaN.
    float t1 = q != 0.0f ? c / q : t0;
    if (t0 > t1) {
        const float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    if (t0 > r.tmin && t0 <= t_hi) {
        t = t0;
        return true;
    }
    if (t1 > r.tmin && t1 <= t_hi) {
        t = t1;
        return true;
    }
    return false;
}

/// Two-sided watertight ray-triangle test (Woop et al. 2013, PBRT v4 Sec. 6.5.3).
///
/// Edges and vertices are inclusive, so a ray through an edge shared by two triangles hits
/// both. Zero-area triangles and rays with a zero determinant miss.
/// @param t_hi Inclusive upper bound; hits with `r.tmin < t <= t_hi` count.
/// @param[out] t Hit distance; unchanged on a miss.
/// @param[out] u, v Barycentric weights of `v1` and `v2`, each in `[0, 1]` with `u + v <= 1`;
///                  unchanged on a miss.
/// @pre `r.dir` is nonzero and finite. Non-finite inputs give a miss.
RT_INLINE bool intersect_triangle(const Ray& r, const Triangle& tri, float t_hi, float& t, float& u,
                                  float& v) {
    // Ray space: translate to the origin, make the largest direction axis z, then shear so
    // the ray points along +z. The transform depends only on the ray, so a vertex shared by
    // two triangles lands on identical bits in both (explicit fmaf avoids contraction drift).
    const int kz =
        detail::max_component_index({::fabsf(r.dir.x), ::fabsf(r.dir.y), ::fabsf(r.dir.z)});
    const int kx = kz == 2 ? 0 : kz + 1;
    const int ky = kx == 2 ? 0 : kx + 1;
    const vec3 d = detail::permute(r.dir, kx, ky, kz);
    vec3 p0 = detail::permute(tri.v0 - r.origin, kx, ky, kz);
    vec3 p1 = detail::permute(tri.v1 - r.origin, kx, ky, kz);
    vec3 p2 = detail::permute(tri.v2 - r.origin, kx, ky, kz);
    const float sx = -d.x / d.z;
    const float sy = -d.y / d.z;
    const float sz = 1.0f / d.z;
    p0.x = ::fmaf(sx, p0.z, p0.x);
    p0.y = ::fmaf(sy, p0.z, p0.y);
    p1.x = ::fmaf(sx, p1.z, p1.x);
    p1.y = ::fmaf(sy, p1.z, p1.y);
    p2.x = ::fmaf(sx, p2.z, p2.x);
    p2.y = ::fmaf(sy, p2.z, p2.y);

    float e0, e1, e2;
    detail::edge_functions(p0, p1, p2, e0, e1, e2);
    // Mixed signs: the ray passes outside. Zeros pass, so edges and vertices are inclusive.
    if ((e0 < 0.0f || e1 < 0.0f || e2 < 0.0f) && (e0 > 0.0f || e1 > 0.0f || e2 > 0.0f)) {
        return false;
    }
    const float det = e0 + e1 + e2;
    if (det == 0.0f) return false;

    const float t_scaled = ::fmaf(e2, p2.z * sz, ::fmaf(e1, p1.z * sz, e0 * (p0.z * sz)));
    const float inv_det = 1.0f / det;
    const float th = t_scaled * inv_det;
    // Exact test on the returned distance, so a distance this test reported is reported
    // again when passed back as t_hi. NaN fails both comparisons.
    if (!(th > r.tmin && th <= t_hi)) return false;
    // Checked last because most calls are rejected above. A zero-area triangle can still get
    // here once the shear rounds its vertices apart.
    if (detail::zero_area(tri)) return false;

    const float bu = e1 * inv_det;
    float bv = e2 * inv_det;
    if (bu + bv > 1.0f) bv = 1.0f - bu;  // rounding can push the sum one ulp above 1
    t = th;
    u = bu;
    v = bv;
    return true;
}

}  // namespace rt

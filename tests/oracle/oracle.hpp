/// @file oracle.hpp
/// @brief Test-only double-precision ray-sphere and ray-triangle oracle (BENCHMARKING quality
///        protocol, section 1).
///
/// The oracle shares no code with `trace/intersect.hpp` and uses different algorithms: the sphere
/// is solved geometrically (closest approach of the ray line to the center, then a half chord),
/// and the triangle by plane intersection with barycentrics from cross products, instead of the
/// float code's stable quadratic and watertight shear. Inputs are the float scene values widened
/// to `double` (exact), and every operation is `double`, so the oracle's own rounding error
/// (about 1e-16 relative) is nine orders of magnitude below the float code's (about 6e-8) and is
/// ignored. Nothing here is built into the renderer.
#pragma once

#include <algorithm>
#include <cmath>

#include "core/ray.hpp"
#include "scene/scene.hpp"

namespace rt::oracle {

/// Plain double 3-vector, so the oracle does not use `rt::vec3` (float).
struct D3 {
    double x, y, z;
};

inline D3 widen(const vec3& a) {
    return {a.x, a.y, a.z};
}
inline D3 operator+(D3 a, D3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline D3 operator-(D3 a, D3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline D3 operator*(double s, D3 a) {
    return {s * a.x, s * a.y, s * a.z};
}
inline double dot(D3 a, D3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline D3 cross(D3 a, D3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(D3 a) {
    return std::sqrt(dot(a, a));
}

/// Exact (to double rounding) ray-line / sphere relationship. Distances are ray parameters `t`,
/// not lengths, so they compare directly with `intersect_sphere`'s `t`.
struct SphereResult {
    bool real = false;          ///< The ray's line meets the sphere (`dist <= radius`).
    double t0 = 0.0, t1 = 0.0;  ///< Roots, `t0 <= t1`; valid only if `real`.
    double t_closest = 0.0;     ///< Parameter of the line's closest approach to the center.
    double dist = 0.0;          ///< Distance from the center to the ray's line.
    double half_chord = 0.0;    ///< `sqrt(radius^2 - dist^2)` as a length; 0 if not `real`.
    double dir_length = 0.0;    ///< `|dir|`, to turn lengths into parameters.
    double scale = 0.0;  ///< `(|origin - center| + radius) / |dir|`: largest parameter in play.
};

/// Solves the sphere geometrically. @pre `r.dir` nonzero, `s.radius > 0`.
inline SphereResult sphere(const Ray& r, const Sphere& s) {
    SphereResult out;
    const D3 oc = widen(r.origin) - widen(s.center);
    const D3 d = widen(r.dir);
    const double dd = dot(d, d);
    const double radius = s.radius;
    out.dir_length = std::sqrt(dd);
    out.t_closest = -dot(oc, d) / dd;
    const D3 foot = oc + out.t_closest * d;  // closest point on the line, relative to the center
    out.dist = length(foot);
    out.scale = (length(oc) + radius) / out.dir_length;
    const double h2 = radius * radius - dot(foot, foot);
    if (h2 < 0.0) return out;
    out.real = true;
    out.half_chord = std::sqrt(h2);
    const double dt = out.half_chord / out.dir_length;
    out.t0 = out.t_closest - dt;
    out.t1 = out.t_closest + dt;
    return out;
}

/// Ray-triangle relationship in double.
struct TriangleResult {
    bool degenerate = false;      ///< Zero-area triangle: never a hit.
    bool parallel = false;        ///< `n . dir == 0`: the line never meets the plane; never a hit.
    double t = 0.0;               ///< Ray parameter of the plane intersection.
    double u = 0.0, v = 0.0;      ///< Barycentric weights of `v1` and `v2`.
    double margin = 0.0;          ///< `min(u, v, 1 - u - v)`: negative outside, 0 on an edge.
    double area_perp = 0.0;       ///< Triangle area projected perpendicular to the ray.
    double plane_distance = 0.0;  ///< Distance from the ray's origin to the triangle's plane.
    double reach = 0.0;           ///< `max |vi - origin|`: largest length in play.
    double dir_length = 0.0;      ///< `|dir|`.
};

/// Intersects the ray's line with the triangle's plane and reports where, in barycentrics.
/// @pre `r.dir` nonzero.
inline TriangleResult triangle(const Ray& r, const Triangle& tri) {
    TriangleResult out;
    const D3 o = widen(r.origin);
    const D3 d = widen(r.dir);
    const D3 v0 = widen(tri.v0), v1 = widen(tri.v1), v2 = widen(tri.v2);
    const D3 e1 = v1 - v0, e2 = v2 - v0;
    const D3 n = cross(e1, e2);
    const double nn = dot(n, n);
    out.dir_length = length(d);
    out.reach = std::max({length(v0 - o), length(v1 - o), length(v2 - o)});
    if (nn == 0.0) {
        out.degenerate = true;
        return out;
    }
    out.plane_distance = std::fabs(dot(n, v0 - o)) / std::sqrt(nn);
    const double denom = dot(n, d);
    out.area_perp = 0.5 * std::fabs(denom) / out.dir_length;
    if (denom == 0.0) {
        out.parallel = true;
        return out;
    }
    out.t = dot(n, v0 - o) / denom;
    const D3 p = (o + out.t * d) - v0;
    out.u = dot(cross(p, e2), n) / nn;
    out.v = dot(cross(e1, p), n) / nn;
    out.margin = std::min({out.u, out.v, 1.0 - out.u - out.v});
    return out;
}

}  // namespace rt::oracle

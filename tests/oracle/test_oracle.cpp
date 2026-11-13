/// @file test_oracle.cpp
/// @brief Compares `intersect_sphere` and `intersect_triangle` with the double-precision oracle
///        on seeded random rays (BENCHMARKING quality protocol, section 1).
///
/// Float and double must agree on hit or miss and on the returned distance, except inside a
/// band around edges, silhouettes and interval ends where float rounding can legitimately
/// flip the answer. The band is derived from the float code's error, not tuned per case:
///
/// Notation: eps = 2^-24 is the float unit roundoff. Every constant K below is a safety factor
/// on the first-order bound. They were set so the worst error seen on these seeds uses at most
/// a quarter of the tolerance: on the reference VM (GCC 13, x86-64) the worst error / tolerance
/// ratios were 0.18 for the sphere distance, 0.06 for the triangle distance and 0.09 for the
/// barycentrics (K_S = 32, K_T = 32, K_B = 8). Each run prints them as the "worst ratio" line,
/// so a compiler or CPU with more roundoff (FMA contraction on arm64, say) shows its slack.
///
/// SPHERE. With oc = origin - center and R the radius, let L = |oc| + R. The float code forms
/// the offset f of the ray's closest approach from the center and takes its length; the
/// roundoff in f is at most eps_L = K_S * eps * L (lengths). The discriminant is proportional
/// to R^2 - |f|^2, so it is wrong by at most D = 2 * R * eps_L. Hence:
///   - Hit or miss is undetermined iff |R^2 - dist^2| <= D (a grazing ray); dist is the
///     oracle's distance from the center to the ray's line.
///   - The half chord h = sqrt(R^2 - dist^2) is wrong by at most min(sqrt(D), D / h), the
///     two regimes of sqrt(x + D) - sqrt(x): near tangency the error grows like sqrt(D), far
///     from it like D / (2 sqrt(x)).
///   - A root is wrong by at most tol_t = (that half-chord error) / |dir| + K_T * eps * scale,
///     with scale = L / |dir| the largest parameter in play (closest-approach rounding).
///   - Whether a root is inside (tmin, t_hi] is undetermined iff it lies within tol_t of
///     tmin or t_hi.
///
/// TRIANGLE. Let reach = max |vi - origin| and A = the triangle's area projected perpendicular
/// to the ray. The float code's edge functions come from ray-space coordinates of size at
/// most reach, so each has absolute error about eps * reach^2, and their sum (the
/// determinant) is between 2A and 2*sqrt(3)*A. The barycentrics are therefore wrong by at most
/// tol_b = K_B * eps * reach^2 / A, and the hit distance by tol_t = tol_b * reach / |dir|
/// (parameter units) plus K_T * eps * |t|.
///   - Inside or outside is undetermined iff |margin| <= tol_b, where margin is the oracle's
///     smallest barycentric weight (negative outside).
///   - Whether t is inside (tmin, t_hi] is undetermined iff t is within tol_t of an end.
///
/// Outside the band the float result must equal the oracle's classification and lie within the
/// tolerance. Inside the band any hit or miss is accepted, but a reported hit must still lie
/// within the tolerance of the oracle's answer: ambiguity is allowed in the decision, not in
/// the value. Each category asserts how many cases it compared, how many fell in the band and
/// how many hit, so a generator that stops producing a kind of case fails the test.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include "oracle.hpp"
#include "scene/pcg32.hpp"
#include "trace/intersect.hpp"

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr double kEps = 0x1p-24;  ///< Float unit roundoff.

// Safety factors of the band (see the file comment).
constexpr double kSphereSafety = 32.0;   ///< K_S: roundoff in the closest-approach offset.
constexpr double kTSafety = 32.0;        ///< K_T: roundoff in parameter arithmetic.
constexpr double kTriangleSafety = 8.0;  ///< K_B: roundoff in the edge functions.

/// Largest error / tolerance ratio seen, so the log shows how much slack the band has.
struct Worst {
    double sphere_t = 0.0, triangle_t = 0.0, triangle_b = 0.0;
};
Worst g_worst;

using rt::oracle::D3;

/// Uniform double in `[lo, hi)` from the float generator.
double uniform(rt::Pcg32& rng, double lo, double hi) {
    return lo + (hi - lo) * rng.uniform();
}

/// `2^e` for a uniformly random integer `e` in `[lo, hi]`: scale factors that are exact in float.
double pow2(rt::Pcg32& rng, int lo, int hi) {
    return std::ldexp(1.0, lo + static_cast<int>(rng.next() % static_cast<uint32_t>(hi - lo + 1)));
}

D3 random_vector(rt::Pcg32& rng, double half_extent) {
    return {uniform(rng, -half_extent, half_extent), uniform(rng, -half_extent, half_extent),
            uniform(rng, -half_extent, half_extent)};
}

/// Uniform random unit vector (rejection sampling in the cube).
D3 random_unit(rt::Pcg32& rng) {
    for (;;) {
        const D3 v = random_vector(rng, 1.0);
        const double n = rt::oracle::length(v);
        if (n > 0.1 && n <= 1.0) return (1.0 / n) * v;
    }
}

/// A unit vector perpendicular to @p d.
D3 perpendicular(rt::Pcg32& rng, D3 d) {
    for (;;) {
        const D3 c = rt::oracle::cross(d, random_unit(rng));
        const double n = rt::oracle::length(c);
        if (n > 0.1) return (1.0 / n) * c;
    }
}

rt::vec3 to_float(D3 a) {
    return {static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(a.z)};
}

/// Hex-float dump of a vec3, for pasting a failing case into a fixed test.
std::string hex(rt::vec3 a) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "{%a, %a, %a}", a.x, a.y, a.z);
    return buf;
}
std::string hex(float a) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%a", a);
    return buf;
}

/// Next float above or below @p x, @p ulps times.
float step_ulps(float x, int ulps) {
    for (; ulps > 0; --ulps) x = std::nextafter(x, kInf);
    for (; ulps < 0; ++ulps) x = std::nextafter(x, -kInf);
    return x;
}

// ---------------------------------------------------------------------------------------------
// Sphere
// ---------------------------------------------------------------------------------------------

struct Counts {
    int compared = 0;  ///< Cases outside every band: decision and value both checked.
    int in_band = 0;   ///< Cases with an undetermined decision: value checked if a hit.
    int hits = 0;      ///< Float hits among all cases.
    int misses = 0;    ///< Float misses among all cases.
};

/// Checks one sphere case against the oracle.
void check_sphere(const rt::Ray& r, const rt::Sphere& s, float t_hi, Counts& counts,
                  const std::string& label) {
    const auto o = rt::oracle::sphere(r, s);
    const double radius = s.radius;
    const double length_scale = o.scale * o.dir_length;  // |oc| + R
    const double eps_l = kSphereSafety * kEps * length_scale;
    const double disc_err = 2.0 * radius * eps_l;
    const double gap = radius * radius - o.dist * o.dist;  // > 0: the line crosses the sphere
    const bool grazing = std::fabs(gap) <= disc_err;
    const double h = o.half_chord;
    const double chord_err =
        o.real && h > 0.0 ? std::min(std::sqrt(disc_err), disc_err / h) : std::sqrt(disc_err);
    const double tol_t = chord_err / o.dir_length + kTSafety * kEps * o.scale;

    float t = -1.0f;
    const bool hit = rt::intersect_sphere(r, s, t_hi, t);
    (hit ? counts.hits : counts.misses)++;
    SCOPED_TRACE(label + " center " + hex(s.center) + " radius " + hex(s.radius) + " o " +
                 hex(r.origin) + " d " + hex(r.dir) + " tmin " + hex(r.tmin) + " t_hi " +
                 hex(t_hi));

    // The oracle's answer: the smallest root in (tmin, t_hi].
    double expect = std::numeric_limits<double>::quiet_NaN();
    bool boundary = false;  // a root within tol_t of an interval end
    if (o.real) {
        for (const double root : {o.t0, o.t1}) {
            if (std::fabs(root - r.tmin) <= tol_t || std::fabs(root - t_hi) <= tol_t) {
                boundary = true;
            }
        }
        if (o.t0 > r.tmin && o.t0 <= t_hi) {
            expect = o.t0;
        } else if (o.t1 > r.tmin && o.t1 <= t_hi) {
            expect = o.t1;
        }
    }

    if (grazing || boundary) {
        ++counts.in_band;
        if (hit) {
            // Ambiguous decision, but the value must be near some root (or, for a ray the
            // oracle says just misses, near the closest approach).
            const double a = std::fabs(t - o.t0), b = std::fabs(t - o.t1);
            const double near = o.real ? std::min(a, b) : std::fabs(t - o.t_closest);
            ASSERT_LE(near, tol_t);
            g_worst.sphere_t = std::max(g_worst.sphere_t, near / tol_t);
        }
        return;
    }
    ++counts.compared;
    ASSERT_EQ(hit, !std::isnan(expect));
    if (hit) {
        ASSERT_LE(std::fabs(t - expect), tol_t);
        g_worst.sphere_t = std::max(g_worst.sphere_t, std::fabs(t - expect) / tol_t);
    }
}

/// A ray whose line passes `dist` from the sphere's center, with origin `travel` lengths before
/// the closest approach, in a random direction scaled by a power of two.
rt::Ray ray_at_distance(rt::Pcg32& rng, const rt::Sphere& s, double dist, double travel,
                        float tmin) {
    const D3 dir = random_unit(rng);
    const D3 offset = dist * perpendicular(rng, dir);
    const D3 closest = rt::oracle::widen(s.center) + offset;
    const D3 origin = closest - travel * dir;
    return {to_float(origin), tmin, to_float(pow2(rng, -10, 10) * dir), kInf};
}

}  // namespace

/// Random spheres and rays of every scale: radii from 0.01 to 100, centers up to 1e4 away,
/// direction lengths from 2^-10 to 2^10, random tmin and t_hi. About half the rays hit.
TEST(OracleSphere, RandomRays) {
    rt::Pcg32 rng(2001);
    Counts counts;
    for (int it = 0; it < 30000; ++it) {
        const double extent = it % 4 == 0 ? 1.0e4 : 100.0;
        const rt::Sphere s{to_float(random_vector(rng, extent)),
                           static_cast<float>(std::exp2(uniform(rng, -6.6, 6.6))), 0};
        const double dist = s.radius * uniform(rng, 0.0, 1.6);
        const double travel = uniform(rng, 0.0, 2.0 * extent);
        const float tmin = it % 3 == 0 ? 0.0f : static_cast<float>(uniform(rng, 0.0, 0.5 * travel));
        rt::Ray r = ray_at_distance(rng, s, dist, travel, tmin);
        const float t_hi = it % 5 == 0 ? kInf : static_cast<float>(uniform(rng, 0.0, 3.0 * travel));
        check_sphere(r, s, t_hi, counts, "seed 2001 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.compared, 20000);
    EXPECT_GE(counts.hits, 5000);  // tmin and t_hi turn many geometric hits into misses
    EXPECT_GE(counts.misses, 8000);
    EXPECT_LE(counts.in_band, 2000);
}

/// Rays aimed at the silhouette: the line passes within a few thousand eps of radius. Most
/// land in the band, where either answer is accepted but a hit must still give a good t.
TEST(OracleSphere, GrazingRays) {
    rt::Pcg32 rng(2002);
    Counts counts;
    for (int it = 0; it < 20000; ++it) {
        const rt::Sphere s{to_float(random_vector(rng, 50.0)),
                           static_cast<float>(std::exp2(uniform(rng, -3.0, 3.0))), 0};
        const double relative = uniform(rng, -1.0, 1.0) * std::exp2(-uniform(rng, 8.0, 22.0));
        const double travel = uniform(rng, 1.0, 100.0);
        const rt::Ray r = ray_at_distance(rng, s, s.radius * (1.0 + relative), travel, 0.0f);
        check_sphere(r, s, kInf, counts, "seed 2002 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.in_band, 3000);   // the generator really reaches the band
    EXPECT_GE(counts.compared, 3000);  // and also cases just outside it
    EXPECT_GE(counts.hits, 3000);
    EXPECT_GE(counts.misses, 3000);
}

/// Origins inside the sphere with tmin = 0: the near root is behind the origin, so the hit is
/// the far root.
TEST(OracleSphere, OriginInside) {
    rt::Pcg32 rng(2003);
    Counts counts;
    for (int it = 0; it < 10000; ++it) {
        const rt::Sphere s{to_float(random_vector(rng, 100.0)),
                           static_cast<float>(std::exp2(uniform(rng, -3.0, 5.0))), 0};
        const D3 inside = rt::oracle::widen(s.center) +
                          (s.radius * 0.95 * std::cbrt(rng.uniform())) * random_unit(rng);
        const rt::Ray r{to_float(inside), 0.0f, to_float(pow2(rng, -6, 6) * random_unit(rng)),
                        kInf};
        check_sphere(r, s, kInf, counts, "seed 2003 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.compared, 9500);
    EXPECT_EQ(counts.hits, 10000);  // inside, a ray always leaves through the far root
}

/// tmin or t_hi placed within two ulps of a root: the decision is undetermined, but a hit must
/// still be near a root.
TEST(OracleSphere, IntervalEndsAtRoots) {
    rt::Pcg32 rng(2004);
    Counts counts;
    for (int it = 0; it < 20000; ++it) {
        const rt::Sphere s{to_float(random_vector(rng, 50.0)),
                           static_cast<float>(std::exp2(uniform(rng, -3.0, 3.0))), 0};
        rt::Ray r = ray_at_distance(rng, s, s.radius * uniform(rng, 0.0, 0.9),
                                    uniform(rng, 1.0, 100.0), 0.0f);
        const auto o = rt::oracle::sphere(r, s);
        ASSERT_TRUE(o.real);
        const double root = it % 2 == 0 ? o.t0 : o.t1;
        const float end = step_ulps(static_cast<float>(root), static_cast<int>(rng.next() % 5) - 2);
        float t_hi = kInf;
        if (it % 4 < 2) {
            r.tmin = end;
        } else {
            t_hi = end;
        }
        check_sphere(r, s, t_hi, counts, "seed 2004 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.in_band, 15000);
    EXPECT_GE(counts.hits, 5000);
    EXPECT_GE(counts.misses, 3000);
}

// ---------------------------------------------------------------------------------------------
// Triangle
// ---------------------------------------------------------------------------------------------

namespace {

/// Checks one triangle case against the oracle.
void check_triangle(const rt::Ray& r, const rt::Triangle& tri, float t_hi, Counts& counts,
                    const std::string& label) {
    const auto o = rt::oracle::triangle(r, tri);
    float t = -1.0f, u = -1.0f, v = -1.0f;
    const bool hit = rt::intersect_triangle(r, tri, t_hi, t, u, v);
    (hit ? counts.hits : counts.misses)++;
    SCOPED_TRACE(label + " v0 " + hex(tri.v0) + " v1 " + hex(tri.v1) + " v2 " + hex(tri.v2) +
                 " o " + hex(r.origin) + " d " + hex(r.dir) + " tmin " + hex(r.tmin) + " t_hi " +
                 hex(t_hi));
    if (o.degenerate || (o.parallel && o.plane_distance > 1.0e-3 * o.reach)) {
        // A zero-area triangle has no interior; a ray parallel to the plane and clearly off it
        // never meets it. A ray parallel to the plane and (nearly) in it is coplanar: the float
        // code's answer is not defined by the contract, so it is not judged here.
        ++counts.compared;
        ASSERT_FALSE(hit) << "a zero-area triangle or a ray parallel to and off the plane hit";
        return;
    }
    if (o.parallel) {
        ++counts.in_band;
        return;
    }
    const double tol_b = kTriangleSafety * kEps * o.reach * o.reach / o.area_perp;
    const double tol_t =
        tol_b * o.reach / o.dir_length + kTSafety * kEps * std::fabs(o.t) + 1.0e-300;
    const bool boundary = std::fabs(o.t - r.tmin) <= tol_t || std::fabs(o.t - t_hi) <= tol_t;
    const bool edge = std::fabs(o.margin) <= tol_b;
    const bool inside_interval = o.t > r.tmin && o.t <= t_hi;

    if (edge || boundary) {
        ++counts.in_band;
    } else {
        ++counts.compared;
        ASSERT_EQ(hit, o.margin > 0.0 && inside_interval);
    }
    if (hit) {
        ASSERT_LE(std::fabs(t - o.t), tol_t);
        ASSERT_LE(std::fabs(u - o.u), tol_b);
        ASSERT_LE(std::fabs(v - o.v), tol_b);
        g_worst.triangle_t = std::max(g_worst.triangle_t, std::fabs(t - o.t) / tol_t);
        g_worst.triangle_b =
            std::max(g_worst.triangle_b, std::max(std::fabs(u - o.u), std::fabs(v - o.v)) / tol_b);
    }
}

rt::Triangle random_triangle(rt::Pcg32& rng, double extent, double size) {
    const D3 base = random_vector(rng, extent);
    return {to_float(base), to_float(base + random_vector(rng, size)),
            to_float(base + random_vector(rng, size)), 0};
}

/// A ray through the triangle's point with barycentrics (b1, b2), from `travel` lengths away on
/// a random side, with a direction scaled by a power of two. The float origin rounds, so the
/// ray misses the exact point by a few eps; the oracle judges the rounded ray.
rt::Ray ray_through(rt::Pcg32& rng, const rt::Triangle& tri, double b1, double b2, double travel,
                    float tmin) {
    const D3 v0 = rt::oracle::widen(tri.v0);
    const D3 e1 = rt::oracle::widen(tri.v1) - v0, e2 = rt::oracle::widen(tri.v2) - v0;
    const D3 target = v0 + (b1 * e1 + b2 * e2);
    D3 dir = random_unit(rng);
    // Keep the ray from running nearly parallel to the plane, which would leave nothing to test.
    const D3 n = rt::oracle::cross(e1, e2);
    if (std::fabs(rt::oracle::dot(n, dir)) < 0.2 * rt::oracle::length(n)) {
        dir = (1.0 / rt::oracle::length(n)) * n;
    }
    return {to_float(target - travel * dir), tmin, to_float(pow2(rng, -10, 10) * dir), kInf};
}

}  // namespace

/// Random triangles up to 1e4 from the origin and 0.1 to 100 across; rays aimed anywhere from
/// well outside to well inside, from either side.
TEST(OracleTriangle, RandomRays) {
    rt::Pcg32 rng(3001);
    Counts counts;
    for (int it = 0; it < 30000; ++it) {
        const double extent = it % 4 == 0 ? 1.0e4 : 100.0;
        const rt::Triangle tri = random_triangle(rng, extent, std::exp2(uniform(rng, -3.3, 6.6)));
        const double travel = uniform(rng, 0.5, 2.0 * extent);
        const rt::Ray r =
            ray_through(rng, tri, uniform(rng, -0.4, 1.4), uniform(rng, -0.4, 1.4), travel, 0.0f);
        check_triangle(r, tri, kInf, counts, "seed 3001 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.compared, 15000);  // the band is wide for tiny triangles far away
    EXPECT_GE(counts.hits, 3500);
    EXPECT_GE(counts.misses, 8000);
}

/// Rays aimed within a few thousand eps of an edge or vertex. The oracle's margin says which
/// side the rounded ray is on; most cases land in the band.
TEST(OracleTriangle, NearEdgeRays) {
    rt::Pcg32 rng(3002);
    Counts counts;
    for (int it = 0; it < 30000; ++it) {
        const rt::Triangle tri = random_triangle(rng, 50.0, std::exp2(uniform(rng, -2.0, 4.0)));
        const double along = uniform(rng, 0.0, 1.0);
        const double off = uniform(rng, -1.0, 1.0) * std::exp2(-uniform(rng, 6.0, 22.0));
        double b1, b2;
        switch (it % 3) {
            case 0:
                b1 = along;
                b2 = off;
                break;  // edge v0-v1 (v = off)
            case 1:
                b1 = off;
                b2 = along;
                break;  // edge v0-v2 (u = off)
            default:
                b1 = along;
                b2 = 1.0 - along + off;
                break;  // edge v1-v2 (u + v = 1 + off)
        }
        const rt::Ray r = ray_through(rng, tri, b1, b2, uniform(rng, 1.0, 100.0), 0.0f);
        check_triangle(r, tri, kInf, counts, "seed 3002 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.in_band, 5000);
    EXPECT_GE(counts.compared, 4000);
    EXPECT_GE(counts.hits, 8000);
    EXPECT_GE(counts.misses, 3000);
}

/// tmin or t_hi placed within two ulps of the hit distance: undetermined decision, but a hit
/// must still carry the right t and barycentrics.
TEST(OracleTriangle, IntervalEndsAtHit) {
    rt::Pcg32 rng(3003);
    Counts counts;
    for (int it = 0; it < 20000; ++it) {
        const rt::Triangle tri = random_triangle(rng, 50.0, std::exp2(uniform(rng, -2.0, 4.0)));
        rt::Ray r = ray_through(rng, tri, uniform(rng, 0.1, 0.5), uniform(rng, 0.1, 0.4),
                                uniform(rng, 1.0, 100.0), 0.0f);
        const auto o = rt::oracle::triangle(r, tri);
        ASSERT_FALSE(o.parallel);
        const float end = step_ulps(static_cast<float>(o.t), static_cast<int>(rng.next() % 5) - 2);
        float t_hi = kInf;
        if (it % 2 == 0) {
            r.tmin = end;
        } else {
            t_hi = end;
        }
        check_triangle(r, tri, t_hi, counts, "seed 3003 it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_GE(counts.in_band, 15000);
    EXPECT_GE(counts.hits, 5000);
    EXPECT_GE(counts.misses, 3000);
}

/// Zero-area triangles (exactly collinear vertices) never hit, even for a ray straight through
/// the segment; rays in the plane of a real triangle never hit.
TEST(OracleTriangle, DegenerateAndParallelNeverHit) {
    rt::Pcg32 rng(3004);
    Counts counts;
    for (int it = 0; it < 5000; ++it) {
        // Multiples of 1/8 below 32 are exact in float and double, and so are their sums and
        // differences, so v2 = v0 + 2 (v1 - v0) is exactly collinear in both.
        const auto grid = [&](int range) {
            return static_cast<float>(static_cast<int>(rng.next() % (2 * range + 1)) - range) /
                   8.0f;
        };
        const rt::vec3 a{grid(160), grid(160), grid(160)};
        const rt::vec3 e{grid(40), grid(40), grid(40)};
        const rt::Triangle line{a, a + e, a + 2.0f * e, 0};
        const double point = uniform(rng, 0.0, 1.0);
        const rt::Ray through = ray_through(rng, line, point, 0.0, uniform(rng, 1.0, 10.0), 0.0f);
        check_triangle(through, line, kInf, counts,
                       "seed 3004 degenerate it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
        // A ray exactly parallel to an axis-aligned triangle's plane (the direction has a zero
        // component along the plane's normal axis, so n . dir is exactly 0 in double) and
        // clearly off the plane.
        const int axis = it % 3, a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
        const double plane = uniform(rng, -20.0, 20.0);
        rt::Triangle flat{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 0};
        for (rt::vec3* vertex : {&flat.v0, &flat.v1, &flat.v2}) {
            (*vertex)[axis] = static_cast<float>(plane);
            (*vertex)[a1] = static_cast<float>(uniform(rng, -20.0, 20.0));
            (*vertex)[a2] = static_cast<float>(uniform(rng, -20.0, 20.0));
        }
        rt::vec3 origin{}, dir{};
        origin[axis] =
            static_cast<float>(plane + (it % 2 == 0 ? 1.0 : -1.0) * uniform(rng, 0.5, 10.0));
        origin[a1] = static_cast<float>(uniform(rng, -20.0, 20.0));
        origin[a2] = static_cast<float>(uniform(rng, -20.0, 20.0));
        dir[axis] = 0.0f;
        dir[a1] = static_cast<float>(uniform(rng, -1.0, 1.0));
        dir[a2] = static_cast<float>(uniform(rng, 0.1, 1.0));
        const rt::Ray parallel{origin, 0.0f, dir, kInf};
        ASSERT_TRUE(rt::oracle::triangle(parallel, flat).parallel);  // the setup is as intended
        check_triangle(parallel, flat, kInf, counts, "seed 3004 parallel it " + std::to_string(it));
        if (::testing::Test::HasFatalFailure()) return;
    }
    EXPECT_EQ(counts.hits, 0);
    EXPECT_EQ(counts.compared, 10000);
}

/// A quad of two triangles sharing a diagonal: a ray the oracle puts inside the quad, away from
/// its outer edges, hits at least one triangle, including rays through the diagonal itself
/// (watertight). A ray outside the quad by more than the band hits neither.
TEST(OracleTriangle, SharedEdgeIsWatertight) {
    rt::Pcg32 rng(3005);
    int inside = 0, outside = 0, on_diagonal = 0;
    for (int it = 0; it < 30000; ++it) {
        const D3 a = random_vector(rng, 50.0);
        const D3 side1 = random_vector(rng, 8.0), side2 = random_vector(rng, 8.0);
        // Parallelogram a, a+side1, a+side1+side2, a+side2 split along a -> a+side1+side2.
        const rt::vec3 p0 = to_float(a), p1 = to_float(a + side1);
        const rt::vec3 p2 = to_float(a + side1 + side2), p3 = to_float(a + side2);
        const rt::Triangle t0{p0, p1, p2, 0}, t1{p0, p2, p3, 0};
        // Aim at a point of the quad; every third ray on the shared diagonal itself.
        double s, t;
        if (it % 3 == 0) {
            s = t = uniform(rng, 0.05, 0.95);
        } else {
            s = uniform(rng, -0.3, 1.3);
            t = uniform(rng, -0.3, 1.3);
        }
        const D3 target = a + (s * side1 + t * side2);
        const D3 n = rt::oracle::cross(side1, side2);
        const double nn = rt::oracle::length(n);
        if (nn < 1.0) continue;
        D3 dir = random_unit(rng);
        if (std::fabs(rt::oracle::dot(n, dir)) < 0.2 * nn) dir = (1.0 / nn) * n;
        const double travel = uniform(rng, 1.0, 100.0);
        const rt::Ray r{to_float(target - travel * dir), 0.0f, to_float(dir), kInf};

        const auto o0 = rt::oracle::triangle(r, t0), o1 = rt::oracle::triangle(r, t1);
        if (o0.parallel || o1.parallel) continue;
        // Distance to the quad's outer boundary, in barycentric units of the two triangles:
        // t0 owns the outer edges v0-v1 and v1-v2, t1 owns v2-v3 and v3-v0.
        const double band = kTriangleSafety * kEps * o0.reach * o0.reach / o0.area_perp * 4.0;
        const double outer0 = std::min(o0.v, 1.0 - o0.u - o0.v);  // opposite v0-v1 side: v; v1-v2
        const double outer1 = std::min(o1.u, 1.0 - o1.u - o1.v);  // v0-v2 is shared; v2-v3, v3-v0
        (void)outer0;
        (void)outer1;
        float tt, u, v;
        const bool h0 = rt::intersect_triangle(r, t0, kInf, tt, u, v);
        const bool h1 = rt::intersect_triangle(r, t1, kInf, tt, u, v);
        SCOPED_TRACE("seed 3005 it " + std::to_string(it) + " p0 " + hex(p0) + " p1 " + hex(p1) +
                     " p2 " + hex(p2) + " p3 " + hex(p3) + " o " + hex(r.origin) + " d " +
                     hex(r.dir));
        // Quad coordinates of the rounded ray's point, from the oracle on t0 and t1: a point of
        // t0 has (u, v) in t0; a point of t1 has (u, v) in t1. Inside the quad iff either has
        // margin > band; outside iff both are below -band.
        const bool clearly_in = o0.margin > band || o1.margin > band;
        const bool clearly_out = o0.margin < -band && o1.margin < -band;
        if (clearly_in || it % 3 == 0) {
            ASSERT_TRUE(h0 || h1) << "a ray inside the quad hit neither triangle";
            ++inside;
            if (it % 3 == 0) ++on_diagonal;
        } else if (clearly_out) {
            ASSERT_FALSE(h0 || h1);
            ++outside;
        }
    }
    EXPECT_GE(inside, 10000);
    EXPECT_GE(outside, 3000);
    EXPECT_GE(on_diagonal, 5000);
}

namespace {
/// Prints the largest error / tolerance ratio seen once all tests have run, to show how much
/// slack the band constants leave.
class WorstRatioReport : public ::testing::Environment {
   public:
    void TearDown() override {
        std::printf("oracle band: worst ratio sphere t %.3f, triangle t %.3f, triangle uv %.3f\n",
                    g_worst.sphere_t, g_worst.triangle_t, g_worst.triangle_b);
    }
};
const auto* const kReport = ::testing::AddGlobalTestEnvironment(new WorstRatioReport);
}  // namespace

/// @file test_trace.cpp
/// @brief Tests for src/trace: the Hit record, ray-primitive intersection, and brute force.
// hit.hpp comes first, so this file also checks that it compiles on its own.
#include "trace/hit.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "trace/brute.hpp"
#include "trace/intersect.hpp"

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr int kIterations = 1000;

/// Ray from @p o along @p d over `(tmin, tmax)`.
rt::Ray make_ray(rt::vec3 o, rt::vec3 d, float tmin = 0.0f, float tmax = kInf) {
    return {o, tmin, d, tmax};
}

/// Formats a vec3 with exact hex floats, for pasting a failing case into an example test.
std::string hex(rt::vec3 a) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "{%a, %a, %a}", a.x, a.y, a.z);
    return buf;
}

/// Formats one float as a hex float.
std::string hex(float a) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%a", a);
    return buf;
}

/// Uniform float in `[lo, hi)`.
float uniform(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

/// Uniform point in the box `[-h, h]^3`.
rt::vec3 uniform_vec(std::mt19937& rng, float h) {
    return {uniform(rng, -h, h), uniform(rng, -h, h), uniform(rng, -h, h)};
}

/// Sphere at the origin with radius 1.
const rt::Sphere kUnitSphere{{0.0f, 0.0f, 0.0f}, 1.0f, 0};

/// Right triangle in the z = 0 plane (PrimRef 0x80000000 in scene tests).
const rt::Triangle kT0{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0};

/// Shares the edge (1,0,0)-(0,1,0) with kT0, same winding (PrimRef 0x80000001).
const rt::Triangle kT1{{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0};

/// +z, the direction of every triangle example ray. With it, ray space is a pure translation.
const rt::vec3 kUp{0.0f, 0.0f, 1.0f};

/// Hit members sit at the offsets INTERFACES §5 fixes, in a 16-byte record.
TEST(Hit, Layout) {
    EXPECT_EQ(offsetof(rt::Hit, t), 0u);
    EXPECT_EQ(offsetof(rt::Hit, prim), 4u);
    EXPECT_EQ(offsetof(rt::Hit, u), 8u);
    EXPECT_EQ(offsetof(rt::Hit, v), 12u);
    EXPECT_EQ(sizeof(rt::Hit), 16u);
}

/// The miss constant is the all-ones PrimRef from INTERFACES §5.
TEST(Hit, MissPrim) {
    EXPECT_EQ(rt::RT_MISS_PRIM, 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------------------------
// Sphere examples
// ---------------------------------------------------------------------------------------------

/// A ray from outside through the center hits the near surface.
TEST(IntersectSphere, Hit) {
    float t = -1.0f;
    EXPECT_TRUE(rt::intersect_sphere(make_ray({0, 0, -5}, kUp), kUnitSphere, kInf, t));
    EXPECT_EQ(t, 4.0f);
}

/// A ray passing beside the sphere misses and leaves t unchanged.
TEST(IntersectSphere, Miss) {
    float t = -1.0f;
    EXPECT_FALSE(rt::intersect_sphere(make_ray({0, 2, -5}, kUp), kUnitSphere, kInf, t));
    EXPECT_EQ(t, -1.0f);
}

/// A tangent ray has a zero discriminant and hits at the touching point.
TEST(IntersectSphere, Tangent) {
    float t = -1.0f;
    EXPECT_TRUE(rt::intersect_sphere(make_ray({0, 1, -5}, kUp), kUnitSphere, kInf, t));
    EXPECT_EQ(t, 5.0f);
}

/// A ray starting at the center returns the far root.
TEST(IntersectSphere, OriginInside) {
    float t = -1.0f;
    EXPECT_TRUE(rt::intersect_sphere(make_ray({0, 0, 0}, kUp), kUnitSphere, kInf, t));
    EXPECT_EQ(t, 1.0f);
}

/// A unit sphere 10^4 away is hit at 9999; the textbook discriminant cancels to 0 here.
TEST(IntersectSphere, DistantSphere) {
    const rt::Sphere far_sphere{{0.0f, 0.0f, 1.0e4f}, 1.0f, 0};
    float t = -1.0f;
    EXPECT_TRUE(rt::intersect_sphere(make_ray({0, 0, 0}, kUp), far_sphere, kInf, t));
    EXPECT_NEAR(t, 9999.0f, 1.0e-3f);
}

/// A root exactly at tmin is excluded, so a ray leaving the surface reports the other root.
TEST(IntersectSphere, RootAtTminExcluded) {
    float t = -1.0f;
    EXPECT_TRUE(rt::intersect_sphere(make_ray({0, 0, -1}, kUp), kUnitSphere, kInf, t));
    EXPECT_EQ(t, 2.0f);
}

/// A root exactly at t_hi is included.
TEST(IntersectSphere, RootAtThiIncluded) {
    float t = -1.0f;
    EXPECT_TRUE(rt::intersect_sphere(make_ray({0, 0, -1}, kUp), kUnitSphere, 2.0f, t));
    EXPECT_EQ(t, 2.0f);
    EXPECT_FALSE(rt::intersect_sphere(make_ray({0, 0, -1}, kUp), kUnitSphere, 1.99f, t));
}

// ---------------------------------------------------------------------------------------------
// Triangle examples (dyadic inputs: every expected value is exact)
// ---------------------------------------------------------------------------------------------

/// An interior hit returns the exact distance and barycentrics.
TEST(IntersectTriangle, Hit) {
    float t = -1.0f, u = -1.0f, v = -1.0f;
    EXPECT_TRUE(rt::intersect_triangle(make_ray({0.25f, 0.25f, -1}, kUp), kT0, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
    EXPECT_EQ(u, 0.25f);
    EXPECT_EQ(v, 0.25f);
}

/// A ray outside the triangle misses and leaves the outputs unchanged.
TEST(IntersectTriangle, Miss) {
    float t = -1.0f, u = -1.0f, v = -1.0f;
    EXPECT_FALSE(rt::intersect_triangle(make_ray({0.75f, 0.75f, -1}, kUp), kT0, kInf, t, u, v));
    EXPECT_EQ(t, -1.0f);
    EXPECT_EQ(u, -1.0f);
    EXPECT_EQ(v, -1.0f);
}

/// Both windings hit: there is no backface culling.
TEST(IntersectTriangle, BothWindings) {
    const rt::Triangle flipped{kT0.v0, kT0.v2, kT0.v1, 0};
    const rt::Ray r = make_ray({0.25f, 0.25f, -1}, kUp);
    float t = -1.0f, u, v;
    EXPECT_TRUE(rt::intersect_triangle(r, kT0, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
    t = -1.0f;
    EXPECT_TRUE(rt::intersect_triangle(r, flipped, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
    // A ray from the other side hits too.
    t = -1.0f;
    EXPECT_TRUE(
        rt::intersect_triangle(make_ray({0.25f, 0.25f, 1}, {0, 0, -1}), kT0, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
}

/// Collinear and repeated-vertex triangles never hit, even with a ray through them.
TEST(IntersectTriangle, Degenerate) {
    const rt::Triangle collinear{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, 0};
    const rt::Triangle repeated{{0, 0, 0}, {0, 0, 0}, {0, 1, 0}, 0};
    float t, u, v;
    EXPECT_FALSE(rt::intersect_triangle(make_ray({1, 0, -1}, kUp), collinear, kInf, t, u, v));
    EXPECT_FALSE(rt::intersect_triangle(make_ray({0, 0.5f, -1}, kUp), repeated, kInf, t, u, v));
    // In-plane ray: nonzero area but zero determinant.
    EXPECT_FALSE(rt::intersect_triangle(make_ray({-1, 0.25f, 0}, {1, 0, 0}), kT0, kInf, t, u, v));
}

/// A ray exactly through a shared edge hits both triangles at the same distance.
TEST(IntersectTriangle, SharedEdge) {
    const rt::Ray r = make_ray({0.5f, 0.5f, -1}, kUp);
    float t = -1.0f, u = -1.0f, v = -1.0f;
    EXPECT_TRUE(rt::intersect_triangle(r, kT0, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
    EXPECT_EQ(u, 0.5f);
    EXPECT_EQ(v, 0.5f);
    t = u = v = -1.0f;
    EXPECT_TRUE(rt::intersect_triangle(r, kT1, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
    EXPECT_EQ(u, 0.0f);
    EXPECT_EQ(v, 0.5f);
}

/// A ray exactly through a vertex hits with barycentrics of that vertex.
TEST(IntersectTriangle, Vertex) {
    float t = -1.0f, u = -1.0f, v = -1.0f;
    EXPECT_TRUE(rt::intersect_triangle(make_ray({0, 0, -1}, kUp), kT0, kInf, t, u, v));
    EXPECT_EQ(t, 1.0f);
    EXPECT_EQ(u, 0.0f);
    EXPECT_EQ(v, 0.0f);
}

/// A hit exactly at tmin is rejected (open lower bound).
TEST(IntersectTriangle, HitAtTminRejected) {
    float t, u, v;
    EXPECT_FALSE(
        rt::intersect_triangle(make_ray({0.25f, 0.25f, -1}, kUp, 1.0f), kT0, kInf, t, u, v));
}

/// A hit exactly at t_hi is accepted (inclusive upper bound).
TEST(IntersectTriangle, HitAtThiAccepted) {
    float t = -1.0f, u, v;
    EXPECT_TRUE(rt::intersect_triangle(make_ray({0.25f, 0.25f, -1}, kUp), kT0, 1.0f, t, u, v));
    EXPECT_EQ(t, 1.0f);
}

/// The double fallback runs when a float edge function is exactly 0 (ray on an edge).
TEST(IntersectTriangle, DoubleFallbackRuns) {
    // T0 translated by origin (0.5, 0.5, -1); with dir +z there is no shear.
    float e0, e1, e2;
    EXPECT_TRUE(rt::detail::edge_functions({-0.5f, -0.5f, 1}, {0.5f, -0.5f, 1}, {-0.5f, 0.5f, 1},
                                           e0, e1, e2));
    EXPECT_EQ(e0, 0.0f);
    EXPECT_EQ(e1, 0.5f);
    EXPECT_EQ(e2, 0.5f);
    float t, u, v;
    EXPECT_TRUE(rt::intersect_triangle(make_ray({0.5f, 0.5f, -1}, kUp), kT0, kInf, t, u, v));
}

/// The double fallback is skipped for an interior ray (all edge functions nonzero).
TEST(IntersectTriangle, DoubleFallbackSkipped) {
    float e0, e1, e2;
    EXPECT_FALSE(rt::detail::edge_functions({-0.25f, -0.25f, 1}, {0.75f, -0.25f, 1},
                                            {-0.25f, 0.75f, 1}, e0, e1, e2));
    EXPECT_EQ(e0, 0.5f);
    EXPECT_EQ(e1, 0.25f);
    EXPECT_EQ(e2, 0.25f);
}

// ---------------------------------------------------------------------------------------------
// Property tests (fixed-seed loops)
// ---------------------------------------------------------------------------------------------

/// Double-precision sphere roots, independent of the float code: textbook quadratic in double.
/// @return false if the discriminant is negative.
bool sphere_roots_double(const rt::Ray& r, const rt::Sphere& s, double& t0, double& t1) {
    const double ox = double(r.origin.x) - s.center.x;
    const double oy = double(r.origin.y) - s.center.y;
    const double oz = double(r.origin.z) - s.center.z;
    const double a =
        double(r.dir.x) * r.dir.x + double(r.dir.y) * r.dir.y + double(r.dir.z) * r.dir.z;
    const double b = 2.0 * (ox * r.dir.x + oy * r.dir.y + oz * r.dir.z);
    const double c = ox * ox + oy * oy + oz * oz - double(s.radius) * s.radius;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return false;
    const double sq = std::sqrt(disc);
    t0 = (-b - sq) / (2.0 * a);
    t1 = (-b + sq) / (2.0 * a);
    return true;
}

/// Feature: intersection-brute-force, Property 1: Sphere test agrees with a double-precision
/// reference on random spheres, rays, and intervals away from boundaries and tangency.
TEST(IntersectSphereProperty, MatchesDoubleReference) {
    std::mt19937 rng(101);
    int compared = 0;
    for (int it = 0; it < kIterations; ++it) {
        const float h = (it % 4 == 0) ? 1.0e4f : 100.0f;
        const rt::Sphere s{uniform_vec(rng, h), uniform(rng, 0.1f, 10.0f), 0};
        const rt::vec3 o = uniform_vec(rng, 10.0f);
        // Aim near the sphere so about half the rays hit; scale dir so t is not a distance.
        const rt::vec3 aim = s.center + uniform_vec(rng, 1.5f * s.radius);
        const rt::vec3 d = (aim - o) * uniform(rng, 0.5f, 2.0f) / rt::length(aim - o);
        const double scale = (double(rt::length(o - s.center)) + s.radius) / rt::length(d);
        const float tmin = (it % 3 == 0) ? 0.0f : uniform(rng, 0.0f, float(scale));
        const float t_hi = (it % 5 == 0) ? kInf : uniform(rng, 0.0f, 2.0f * float(scale));
        const rt::Ray r = make_ray(o, d, tmin);

        double r0, r1;
        const bool real = sphere_roots_double(r, s, r0, r1);
        const double margin = 1.0e-3 * scale;
        if (real &&
            (r1 - r0 < margin || std::fabs(r0 - tmin) < margin || std::fabs(r1 - tmin) < margin ||
             std::fabs(r0 - t_hi) < margin || std::fabs(r1 - t_hi) < margin)) {
            continue;  // too close to tangency or an interval boundary for float
        }
        double expect = std::nan("");
        if (real && r0 > tmin && r0 <= t_hi) {
            expect = r0;
        } else if (real && r1 > tmin && r1 <= t_hi) {
            expect = r1;
        }

        float t = -1.0f;
        const bool hit = rt::intersect_sphere(r, s, t_hi, t);
        SCOPED_TRACE("seed 101 it " + std::to_string(it) + " center " + hex(s.center) + " radius " +
                     hex(s.radius) + " o " + hex(o) + " d " + hex(d) + " tmin " + hex(tmin) +
                     " t_hi " + hex(t_hi));
        ASSERT_EQ(hit, !std::isnan(expect));
        if (hit) {
            EXPECT_NEAR(t, expect, 1.0e-5 * scale);
        }
        ++compared;
    }
    EXPECT_GT(compared, kIterations / 2);
}

/// Feature: intersection-brute-force, Property 2: A ray starting inside a sphere returns the
/// far root: a point on the surface where the ray is leaving.
TEST(IntersectSphereProperty, InsideReturnsFarRoot) {
    std::mt19937 rng(102);
    for (int it = 0; it < kIterations; ++it) {
        const rt::Sphere s{uniform_vec(rng, 100.0f), uniform(rng, 0.1f, 10.0f), 0};
        rt::vec3 off = uniform_vec(rng, 1.0f);
        const float len = rt::length(off);
        if (len < 1.0e-3f) continue;
        off = off * (uniform(rng, 0.0f, 0.99f) * s.radius / len);
        const rt::vec3 o = s.center + off;
        const rt::vec3 d = uniform_vec(rng, 1.0f);
        if (rt::length(d) < 1.0e-2f) continue;

        float t = -1.0f;
        SCOPED_TRACE("seed 102 it " + std::to_string(it) + " o " + hex(o) + " d " + hex(d));
        ASSERT_TRUE(rt::intersect_sphere(make_ray(o, d), s, kInf, t));
        EXPECT_GT(t, 0.0f);
        const rt::vec3 p = o + t * d;
        EXPECT_NEAR(rt::length(p - s.center), s.radius, 1.0e-4f * (s.radius + 100.0f));
        EXPECT_GT(rt::dot(p - s.center, d), 0.0f);
    }
}

/// Random non-degenerate triangle with vertices in `[-10, 10]^3` and area at least 1.
rt::Triangle random_triangle(std::mt19937& rng) {
    for (;;) {
        const rt::Triangle tri{uniform_vec(rng, 10.0f), uniform_vec(rng, 10.0f),
                               uniform_vec(rng, 10.0f), 0};
        if (0.5f * rt::length(rt::cross(tri.v1 - tri.v0, tri.v2 - tri.v0)) >= 1.0f) return tri;
    }
}

/// A random ray aimed at barycentric target `(b1, b2)` of @p tri so that `t == 1` there.
/// The origin is at least 0.2 (in cosine) off the plane.
rt::Ray ray_at_target(std::mt19937& rng, const rt::Triangle& tri, float b1, float b2) {
    const rt::vec3 n = rt::normalize(rt::cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
    const rt::vec3 p = (1.0f - b1 - b2) * tri.v0 + b1 * tri.v1 + b2 * tri.v2;
    for (;;) {
        const rt::vec3 w = uniform_vec(rng, 10.0f);
        const float lw = rt::length(w);
        if (lw < 0.5f || std::fabs(rt::dot(w, n)) < 0.2f * lw) continue;
        return make_ray(p - w, w);
    }
}

/// Feature: intersection-brute-force, Property 3: A distance the test reported is reported
/// again, bit for bit, when passed back as t_hi.
TEST(IntersectProperty, ReReportsAtThi) {
    std::mt19937 rng(103);
    int sphere_hits = 0, triangle_hits = 0;
    for (int it = 0; it < kIterations; ++it) {
        const rt::Sphere s{uniform_vec(rng, 10.0f), uniform(rng, 0.1f, 5.0f), 0};
        // Aim near the sphere, with some origins inside it, so most rays hit.
        const rt::vec3 os = uniform_vec(rng, 20.0f);
        const rt::Ray rs = make_ray(os, s.center + uniform_vec(rng, 1.2f * s.radius) - os);
        float t1, t2;
        if (rt::intersect_sphere(rs, s, kInf, t1)) {
            ++sphere_hits;
            ASSERT_TRUE(rt::intersect_sphere(rs, s, t1, t2)) << "it " << it;
            EXPECT_EQ(t1, t2);
        }

        const rt::Triangle tri = random_triangle(rng);
        const rt::Ray rt_ =
            ray_at_target(rng, tri, uniform(rng, 0.0f, 1.0f), uniform(rng, 0.0f, 1.0f));
        float u1, v1, u2, v2;
        if (rt::intersect_triangle(rt_, tri, kInf, t1, u1, v1)) {
            ++triangle_hits;
            ASSERT_TRUE(rt::intersect_triangle(rt_, tri, t1, t2, u2, v2)) << "it " << it;
            EXPECT_EQ(t1, t2);
            EXPECT_EQ(u1, u2);
            EXPECT_EQ(v1, v2);
        }
    }
    EXPECT_GT(sphere_hits, 100);
    EXPECT_GT(triangle_hits, 100);
}

/// Feature: intersection-brute-force, Property 4: Triangle hits land on the aimed point with
/// valid barycentrics, and rays aimed clearly outside miss.
TEST(IntersectTriangleProperty, HitsAimedPoint) {
    std::mt19937 rng(104);
    int hits = 0, misses = 0;
    for (int it = 0; it < kIterations; ++it) {
        const rt::Triangle tri = random_triangle(rng);
        const float b1 = uniform(rng, -0.2f, 1.2f);
        const float b2 = uniform(rng, -0.2f, 1.2f);
        const float b0 = 1.0f - b1 - b2;
        const float inside = std::fmin(b0, std::fmin(b1, b2));
        if (std::fabs(inside) < 1.0e-3f) continue;  // too close to an edge to classify
        const rt::Ray r = ray_at_target(rng, tri, b1, b2);

        float t = -1.0f, u = -1.0f, v = -1.0f;
        const bool hit = rt::intersect_triangle(r, tri, kInf, t, u, v);
        SCOPED_TRACE("seed 104 it " + std::to_string(it) + " v0 " + hex(tri.v0) + " v1 " +
                     hex(tri.v1) + " v2 " + hex(tri.v2) + " o " + hex(r.origin) + " d " +
                     hex(r.dir));
        ASSERT_EQ(hit, inside > 0.0f);
        if (!hit) {
            ++misses;
            continue;
        }
        ++hits;
        EXPECT_NEAR(t, 1.0f, 1.0e-4f);
        EXPECT_NEAR(u, b1, 1.0e-4f);
        EXPECT_NEAR(v, b2, 1.0e-4f);
        EXPECT_GE(u, 0.0f);
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(u, 1.0f);
        EXPECT_LE(v, 1.0f);
        EXPECT_LE(u + v, 1.0f);
    }
    EXPECT_GT(hits, 100);
    EXPECT_GT(misses, 100);
}

/// Feature: intersection-brute-force, Property 5: The triangle test is two-sided: swapping
/// v1 and v2 keeps the hit and distance and swaps the barycentrics.
TEST(IntersectTriangleProperty, TwoSided) {
    std::mt19937 rng(105);
    for (int it = 0; it < kIterations; ++it) {
        const rt::Triangle tri = random_triangle(rng);
        const rt::Triangle flipped{tri.v0, tri.v2, tri.v1, 0};
        const float b1 = uniform(rng, -0.2f, 1.2f);
        const float b2 = uniform(rng, -0.2f, 1.2f);
        if (std::fabs(std::fmin(1.0f - b1 - b2, std::fmin(b1, b2))) < 1.0e-3f) continue;
        const rt::Ray r = ray_at_target(rng, tri, b1, b2);

        float t = -1.0f, u = -1.0f, v = -1.0f, tf = -1.0f, uf = -1.0f, vf = -1.0f;
        const bool hit = rt::intersect_triangle(r, tri, kInf, t, u, v);
        SCOPED_TRACE("seed 105 it " + std::to_string(it));
        ASSERT_EQ(hit, rt::intersect_triangle(r, flipped, kInf, tf, uf, vf));
        if (!hit) continue;
        EXPECT_NEAR(tf, t, 1.0e-6f * t);
        EXPECT_NEAR(uf, v, 1.0e-6f);
        EXPECT_NEAR(vf, u, 1.0e-6f);
    }
}

/// Feature: intersection-brute-force, Property 7: Triangles with two or three identical
/// vertices never hit, for any ray.
TEST(IntersectTriangleProperty, DegenerateNeverHits) {
    std::mt19937 rng(107);
    for (int it = 0; it < kIterations; ++it) {
        const rt::vec3 a = uniform_vec(rng, 10.0f);
        const rt::vec3 b = uniform_vec(rng, 10.0f);
        const rt::Triangle cases[] = {{a, a, b, 0}, {a, b, a, 0}, {b, a, a, 0}, {a, a, a, 0}};
        // Aim at a point on the segment, where a real hit would be.
        const rt::vec3 target = a + uniform(rng, 0.0f, 1.0f) * (b - a);
        const rt::vec3 o = uniform_vec(rng, 20.0f);
        const rt::Ray r = make_ray(o, target - o);
        for (const rt::Triangle& tri : cases) {
            float t, u, v;
            EXPECT_FALSE(rt::intersect_triangle(r, tri, kInf, t, u, v)) << "it " << it;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Brute force: helpers
// ---------------------------------------------------------------------------------------------

/// Scene view over the given arrays; empty arrays get a null pointer, as SceneHost::view does.
rt::Scene make_scene(const std::vector<rt::Sphere>& spheres,
                     const std::vector<rt::Triangle>& triangles) {
    rt::Scene s{};
    s.spheres = spheres.empty() ? nullptr : spheres.data();
    s.num_spheres = static_cast<uint32_t>(spheres.size());
    s.triangles = triangles.empty() ? nullptr : triangles.data();
    s.num_triangles = static_cast<uint32_t>(triangles.size());
    return s;
}

/// Expects two Hits to match bit for bit in every field.
void expect_same_hit(const rt::Hit& a, const rt::Hit& b) {
    EXPECT_EQ(a.t, b.t);
    EXPECT_EQ(a.prim, b.prim);
    EXPECT_EQ(a.u, b.u);
    EXPECT_EQ(a.v, b.v);
}

/// One primitive's report for a ray, tested with `t_hi = ray.tmax`.
struct Candidate {
    float t, u, v;
    rt::PrimRef prim;
};

/// Every primitive that reports a hit in `(tmin, tmax)`, tested independently of the others.
std::vector<Candidate> candidates(const rt::Ray& r, const rt::Scene& s) {
    std::vector<Candidate> out;
    for (uint32_t i = 0; i < s.num_spheres; ++i) {
        float t;
        if (rt::intersect_sphere(r, s.spheres[i], r.tmax, t) && t < r.tmax) {
            out.push_back({t, 0.0f, 0.0f, i});
        }
    }
    for (uint32_t i = 0; i < s.num_triangles; ++i) {
        float t, u, v;
        if (rt::intersect_triangle(r, s.triangles[i], r.tmax, t, u, v) && t < r.tmax) {
            out.push_back({t, u, v, rt::PRIM_TRIANGLE_BIT | i});
        }
    }
    return out;
}

/// Reference closest hit: the minimum by `(t, PrimRef)` over all candidates.
rt::Hit reference_closest(const rt::Ray& r, const rt::Scene& s) {
    rt::Hit best{r.tmax, rt::RT_MISS_PRIM, 0.0f, 0.0f};
    for (const Candidate& c : candidates(r, s)) {
        if (best.prim == rt::RT_MISS_PRIM || c.t < best.t ||
            (c.t == best.t && c.prim < best.prim)) {
            best = {c.t, c.prim, c.u, c.v};
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Brute force: examples
// ---------------------------------------------------------------------------------------------

/// A hit exactly at tmax is rejected by both loops (open upper bound).
TEST(Brute, HitAtTmaxRejected) {
    const std::vector<rt::Triangle> tris{kT0};
    const rt::Scene s = make_scene({}, tris);
    const rt::Ray r = make_ray({0.25f, 0.25f, -1}, kUp, 0.0f, 1.0f);
    expect_same_hit(rt::brute_closest(r, s), {1.0f, rt::RT_MISS_PRIM, 0.0f, 0.0f});
    EXPECT_FALSE(rt::brute_any(r, s));
}

/// A sphere and a triangle at the same distance resolve to the sphere (PrimRef 0 is lower).
TEST(Brute, SphereTriangleTie) {
    const std::vector<rt::Sphere> spheres{{{0.25f, 0.25f, 1.0f}, 1.0f, 0}};  // near root t = 1
    const std::vector<rt::Triangle> tris{kT0};
    const rt::Ray r = make_ray({0.25f, 0.25f, -1}, kUp);
    expect_same_hit(rt::brute_closest(r, make_scene(spheres, tris)), {1.0f, 0u, 0.0f, 0.0f});
}

/// Two triangles hit at their shared edge resolve to the lower PrimRef.
TEST(Brute, SharedEdgeTie) {
    const std::vector<rt::Triangle> tris{kT0, kT1};
    const rt::Ray r = make_ray({0.5f, 0.5f, -1}, kUp);
    expect_same_hit(rt::brute_closest(r, make_scene({}, tris)),
                    {1.0f, rt::PRIM_TRIANGLE_BIT, 0.5f, 0.5f});
    // Reversing the arrays gives the other triangle the low PrimRef; it now wins the tie.
    const std::vector<rt::Triangle> reversed{kT1, kT0};
    expect_same_hit(rt::brute_closest(r, make_scene({}, reversed)),
                    {1.0f, rt::PRIM_TRIANGLE_BIT, 0.0f, 0.5f});
}

/// An empty scene with null arrays is a miss at tmax, and nothing occludes.
TEST(Brute, EmptyScene) {
    const rt::Scene s{};
    expect_same_hit(rt::brute_closest(make_ray({0, 0, 0}, kUp), s),
                    {kInf, rt::RT_MISS_PRIM, 0.0f, 0.0f});
    expect_same_hit(rt::brute_closest(make_ray({0, 0, 0}, kUp, 0.0f, 7.0f), s),
                    {7.0f, rt::RT_MISS_PRIM, 0.0f, 0.0f});
    EXPECT_FALSE(rt::brute_any(make_ray({0, 0, 0}, kUp), s));
}

/// The nearer of two primitives wins, whichever array it is in.
TEST(Brute, NearestWins) {
    const std::vector<rt::Sphere> spheres{{{0.25f, 0.25f, 4.0f}, 1.0f, 0}};  // near root t = 4
    const std::vector<rt::Triangle> tris{kT0};                               // t = 1
    const rt::Ray r = make_ray({0.25f, 0.25f, -1}, kUp);
    expect_same_hit(rt::brute_closest(r, make_scene(spheres, tris)),
                    {1.0f, rt::PRIM_TRIANGLE_BIT, 0.25f, 0.25f});
    // Past the triangle, the sphere is the closest hit.
    expect_same_hit(
        rt::brute_closest(make_ray({0.25f, 0.25f, -1}, kUp, 1.5f), make_scene(spheres, tris)),
        {4.0f, 0u, 0.0f, 0.0f});
}

/// brute_any: occluded, unoccluded, and an occluder beyond tmax.
TEST(Brute, AnyHit) {
    const std::vector<rt::Triangle> tris{kT0};
    const rt::Scene s = make_scene({}, tris);
    EXPECT_TRUE(rt::brute_any(make_ray({0.25f, 0.25f, -1}, kUp), s));
    EXPECT_FALSE(rt::brute_any(make_ray({0.75f, 0.75f, -1}, kUp), s));
    EXPECT_FALSE(rt::brute_any(make_ray({0.25f, 0.25f, -1}, kUp, 0.0f, 0.5f), s));
}

/// BruteQuery forwards to brute_closest and brute_any.
TEST(Brute, QueryForwards) {
    const std::vector<rt::Sphere> spheres{kUnitSphere};
    const std::vector<rt::Triangle> tris{kT0, kT1};
    const rt::Scene s = make_scene(spheres, tris);
    const rt::BruteQuery q{s};
    for (const rt::Ray& r :
         {make_ray({0.5f, 0.5f, -1}, kUp), make_ray({0, 0, -5}, kUp), make_ray({5, 5, -1}, kUp)}) {
        expect_same_hit(q.closest(r), rt::brute_closest(r, s));
        EXPECT_EQ(q.any(r), rt::brute_any(r, s));
    }
}

/// Presenting the same primitives in a different array order gives the same hit.
TEST(Brute, ReorderedArrays) {
    const rt::Sphere a{{0.25f, 0.25f, 4.0f}, 1.0f, 0};                   // t = 4
    const rt::Sphere b{{9.0f, 9.0f, 9.0f}, 1.0f, 0};                     // not on the ray
    const rt::Triangle far_tri{{-1, -1, 2}, {2, -1, 2}, {-1, 2, 2}, 0};  // t = 3
    const std::vector<rt::Sphere> s1{a, b}, s2{b, a};
    const std::vector<rt::Triangle> t1{far_tri, kT0}, t2{kT0, far_tri};
    const rt::Ray r = make_ray({0.25f, 0.25f, -1}, kUp);
    const rt::Hit h1 = rt::brute_closest(r, make_scene(s1, t1));
    const rt::Hit h2 = rt::brute_closest(r, make_scene(s2, t2));
    // kT0 is triangle 1 in the first order and triangle 0 in the second.
    expect_same_hit(h1, {1.0f, rt::PRIM_TRIANGLE_BIT | 1u, 0.25f, 0.25f});
    expect_same_hit(h2, {1.0f, rt::PRIM_TRIANGLE_BIT | 0u, 0.25f, 0.25f});
}

// ---------------------------------------------------------------------------------------------
// Brute force: properties
// ---------------------------------------------------------------------------------------------

/// Random scene of 0 to 8 spheres and 0 to 8 triangles. About a quarter of the primitives
/// duplicate an earlier one, so exact ties occur.
void random_scene(std::mt19937& rng, std::vector<rt::Sphere>& spheres,
                  std::vector<rt::Triangle>& tris) {
    spheres.clear();
    tris.clear();
    const int ns = static_cast<int>(rng() % 9u);
    const int nt = static_cast<int>(rng() % 9u);
    for (int i = 0; i < ns; ++i) {
        if (i > 0 && rng() % 4u == 0) {
            spheres.push_back(spheres[rng() % spheres.size()]);
        } else {
            spheres.push_back({uniform_vec(rng, 10.0f), uniform(rng, 0.5f, 3.0f), 0});
        }
    }
    for (int i = 0; i < nt; ++i) {
        if (i > 0 && rng() % 4u == 0) {
            tris.push_back(tris[rng() % tris.size()]);
        } else {
            tris.push_back(random_triangle(rng));
        }
    }
}

/// Random ray aimed near one of the scene's primitives, with tmin in {0, 1e-3} and tmax
/// either +inf or a random finite value.
rt::Ray random_scene_ray(std::mt19937& rng, const std::vector<rt::Sphere>& spheres,
                         const std::vector<rt::Triangle>& tris) {
    rt::vec3 aim = uniform_vec(rng, 10.0f);
    const size_t n = spheres.size() + tris.size();
    if (n > 0) {
        const size_t k = rng() % n;
        aim = k < spheres.size() ? spheres[k].center
                                 : (tris[k - spheres.size()].v0 + tris[k - spheres.size()].v1 +
                                    tris[k - spheres.size()].v2) /
                                       3.0f;
        aim = aim + uniform_vec(rng, 1.0f);
    }
    const rt::vec3 o = uniform_vec(rng, 20.0f);
    const float tmin = rng() % 2u ? 0.0f : 1.0e-3f;
    const float tmax = rng() % 2u ? kInf : uniform(rng, 0.1f, 2.0f);
    return make_ray(o, aim - o, tmin, tmax);
}

/// Feature: intersection-brute-force, Property 8: Folding the acceptance rule over candidates
/// in any order gives the minimum by (t, PrimRef) over those inside (tmin, tmax).
TEST(BruteProperty, AcceptIsOrderIndependent) {
    std::mt19937 rng(108);
    const rt::Ray r = make_ray({0, 0, 0}, kUp, 0.5f, 3.0f);
    const float ts[] = {0.25f, 0.5f, 1.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};  // includes tmin, tmax
    for (int it = 0; it < kIterations; ++it) {
        std::vector<std::pair<float, rt::PrimRef>> cands;
        std::vector<rt::PrimRef> pool{0u, 1u, 2u, 7u, 0x80000000u, 0x80000001u, 0x80000005u};
        std::shuffle(pool.begin(), pool.end(), rng);
        const size_t n = rng() % (pool.size() + 1);
        for (size_t i = 0; i < n; ++i) cands.push_back({ts[rng() % 8u], pool[i]});

        const auto fold = [&](const std::vector<std::pair<float, rt::PrimRef>>& list) {
            rt::Hit best{r.tmax, rt::RT_MISS_PRIM, 0.0f, 0.0f};
            bool found = false;
            for (const auto& c : list) {
                if (rt::detail::accept(r, found, best, c.first, c.second)) {
                    best = {c.first, c.second, 0.0f, 0.0f};
                    found = true;
                }
            }
            return best;
        };
        rt::Hit expect{r.tmax, rt::RT_MISS_PRIM, 0.0f, 0.0f};
        for (const auto& c : cands) {
            if (c.first > r.tmin && c.first < r.tmax &&
                (expect.prim == rt::RT_MISS_PRIM || c.first < expect.t ||
                 (c.first == expect.t && c.second < expect.prim))) {
                expect = {c.first, c.second, 0.0f, 0.0f};
            }
        }
        SCOPED_TRACE("seed 108 it " + std::to_string(it));
        const rt::Hit forward = fold(cands);
        std::shuffle(cands.begin(), cands.end(), rng);
        expect_same_hit(forward, expect);
        expect_same_hit(fold(cands), expect);
    }
}

/// Feature: intersection-brute-force, Property 9: brute_closest is bit-identical to the
/// reference minimum over independently tested primitives.
TEST(BruteProperty, MatchesReferenceMinimum) {
    std::mt19937 rng(109);
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> tris;
    int hits = 0;
    for (int it = 0; it < kIterations; ++it) {
        random_scene(rng, spheres, tris);
        const rt::Scene s = make_scene(spheres, tris);
        const rt::Ray r = random_scene_ray(rng, spheres, tris);
        SCOPED_TRACE("seed 109 it " + std::to_string(it));
        const rt::Hit h = rt::brute_closest(r, s);
        expect_same_hit(h, reference_closest(r, s));
        hits += h.prim != rt::RT_MISS_PRIM;
    }
    EXPECT_GT(hits, kIterations / 4);
}

/// Feature: intersection-brute-force, Property 10: Permuting the scene arrays does not change
/// the hit distance or occlusion; a unique winner maps back to the same primitive.
TEST(BruteProperty, ReorderInvariant) {
    std::mt19937 rng(110);
    std::vector<rt::Sphere> spheres, ps;
    std::vector<rt::Triangle> tris, pt;
    for (int it = 0; it < kIterations; ++it) {
        random_scene(rng, spheres, tris);
        const rt::Ray r = random_scene_ray(rng, spheres, tris);
        // perm_s[i] is the original index of permuted sphere i (likewise for triangles).
        std::vector<uint32_t> perm_s(spheres.size()), perm_t(tris.size());
        for (uint32_t i = 0; i < perm_s.size(); ++i) perm_s[i] = i;
        for (uint32_t i = 0; i < perm_t.size(); ++i) perm_t[i] = i;
        std::shuffle(perm_s.begin(), perm_s.end(), rng);
        std::shuffle(perm_t.begin(), perm_t.end(), rng);
        ps.clear();
        pt.clear();
        for (uint32_t i : perm_s) ps.push_back(spheres[i]);
        for (uint32_t i : perm_t) pt.push_back(tris[i]);
        const rt::Scene s = make_scene(spheres, tris);
        const rt::Scene sp = make_scene(ps, pt);

        SCOPED_TRACE("seed 110 it " + std::to_string(it));
        const rt::Hit h = rt::brute_closest(r, s);
        const rt::Hit hp = rt::brute_closest(r, sp);
        EXPECT_EQ(rt::brute_any(r, s), rt::brute_any(r, sp));
        EXPECT_EQ(h.t, hp.t);
        ASSERT_EQ(h.prim == rt::RT_MISS_PRIM, hp.prim == rt::RT_MISS_PRIM);
        if (h.prim == rt::RT_MISS_PRIM) continue;

        int tied = 0;
        for (const Candidate& c : candidates(r, s)) tied += c.t == h.t;
        if (tied == 1) {
            const uint32_t idx = hp.prim & ~rt::PRIM_TRIANGLE_BIT;
            const uint32_t orig = (hp.prim & rt::PRIM_TRIANGLE_BIT) ? perm_t[idx] : perm_s[idx];
            EXPECT_EQ(orig | (hp.prim & rt::PRIM_TRIANGLE_BIT), h.prim);
            EXPECT_EQ(h.u, hp.u);
            EXPECT_EQ(h.v, hp.v);
        } else {
            // Ties: each scene's winner is the lowest PrimRef among its own tied primitives.
            rt::PrimRef lowest = rt::RT_MISS_PRIM;
            for (const Candidate& c : candidates(r, sp)) {
                if (c.t == hp.t && c.prim < lowest) lowest = c.prim;
            }
            EXPECT_EQ(hp.prim, lowest);
        }
    }
}

/// Feature: intersection-brute-force, Property 11: brute_any agrees with brute_closest, and
/// BruteQuery forwards both.
TEST(BruteProperty, AnyAgreesWithClosest) {
    std::mt19937 rng(111);
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> tris;
    for (int it = 0; it < kIterations; ++it) {
        random_scene(rng, spheres, tris);
        const rt::Scene s = make_scene(spheres, tris);
        const rt::Ray r = random_scene_ray(rng, spheres, tris);
        const rt::BruteQuery q{s};
        SCOPED_TRACE("seed 111 it " + std::to_string(it));
        const rt::Hit h = rt::brute_closest(r, s);
        EXPECT_EQ(rt::brute_any(r, s), h.prim != rt::RT_MISS_PRIM);
        expect_same_hit(q.closest(r), h);
        EXPECT_EQ(q.any(r), rt::brute_any(r, s));
    }
}

/// Random unit vector, rejection-sampled from the unit ball.
rt::vec3 random_unit(std::mt19937& rng) {
    for (;;) {
        const rt::vec3 a = uniform_vec(rng, 1.0f);
        const float l = rt::length(a);
        if (l > 0.1f && l <= 1.0f) return a / l;
    }
}

/// A ray ending at @p target (`t == 1`), from at least 0.1 off the plane with normal @p n and
/// at least 0.2 in cosine from it.
rt::Ray ray_to_point(std::mt19937& rng, rt::vec3 target, rt::vec3 n) {
    for (;;) {
        const rt::vec3 w = random_unit(rng) * uniform(rng, 0.5f, 10.0f);
        const float c = std::fabs(rt::dot(w, n));
        if (c >= 0.2f * rt::length(w) && c >= 0.1f) return make_ray(target - w, w);
    }
}

/// Feature: intersection-brute-force, Property 6: Rays through a shared edge of a split quad,
/// or through the center vertex of a triangle fan, never pass between the triangles.
TEST(BruteProperty, SharedEdgesAreWatertight) {
    std::mt19937 rng(106);
    constexpr float kTwoPi = 6.28318531f;
    for (int it = 0; it < kIterations; ++it) {
        const rt::vec3 c = uniform_vec(rng, 10.0f);
        const rt::vec3 n = random_unit(rng);
        const rt::vec3 e1 = rt::normalize(
            rt::cross(n, std::fabs(n.x) < 0.9f ? rt::vec3{1, 0, 0} : rt::vec3{0, 1, 0}));
        const rt::vec3 e2 = rt::cross(n, e1);
        const auto on_plane = [&](float angle, float radius) {
            return c + (radius * std::cos(angle)) * e1 + (radius * std::sin(angle)) * e2;
        };

        // Quad with one corner per quadrant on a common circle, which keeps it convex, split
        // along the diagonal q0-q2. A non-convex quad would put both triangles on one side of
        // the diagonal, and a target rounded off it could then rightly miss both.
        rt::vec3 q[4];
        const float radius = uniform(rng, 1.0f, 3.0f);
        for (int k = 0; k < 4; ++k) {
            q[k] = on_plane(kTwoPi * (k + uniform(rng, 0.1f, 0.9f)) / 4.0f, radius);
        }
        const std::vector<rt::Triangle> quad{{q[0], q[1], q[2], 0}, {q[0], q[2], q[3], 0}};
        const rt::vec3 on_edge = q[0] + uniform(rng, 0.01f, 0.99f) * (q[2] - q[0]);
        const rt::Ray rq = ray_to_point(rng, on_edge, n);
        SCOPED_TRACE("seed 106 it " + std::to_string(it) + " quad o " + hex(rq.origin) + " d " +
                     hex(rq.dir));
        EXPECT_NE(rt::brute_closest(rq, make_scene({}, quad)).prim, rt::RT_MISS_PRIM);

        // Fan of 3 to 8 triangles around c, sharing c and each spoke bit for bit. The jitter
        // keeps every sector below 180 degrees (at most 1.4 * 360 / k), so the fan surrounds c.
        const int k = 3 + static_cast<int>(rng() % 6u);
        std::vector<rt::vec3> rim;
        for (int i = 0; i < k; ++i) {
            rim.push_back(on_plane(kTwoPi * (i + uniform(rng, 0.3f, 0.7f)) / float(k),
                                   uniform(rng, 1.0f, 3.0f)));
        }
        std::vector<rt::Triangle> fan;
        for (int i = 0; i < k; ++i) fan.push_back({c, rim[i], rim[(i + 1) % k], 0});
        const rt::Scene fs = make_scene({}, fan);
        const rt::Ray rc = ray_to_point(rng, c, n);
        EXPECT_NE(rt::brute_closest(rc, fs).prim, rt::RT_MISS_PRIM) << "fan center, k " << k;
        const rt::vec3 spoke = c + uniform(rng, 0.01f, 0.99f) * (rim[rng() % k] - c);
        EXPECT_NE(rt::brute_closest(ray_to_point(rng, spoke, n), fs).prim, rt::RT_MISS_PRIM)
            << "fan spoke, k " << k;
    }
}

}  // namespace

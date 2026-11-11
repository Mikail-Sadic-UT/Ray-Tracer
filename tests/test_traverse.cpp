/// @file test_traverse.cpp
/// @brief BVH traversal tests: examples, overflow, and bit-identical agreement with brute force.
// traverse.hpp must stay the first include, so this file checks that it compiles on its own.
#include "trace/traverse.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "bench/timings.hpp"
#include "bvh/bvh.hpp"
#include "trace/brute.hpp"

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

/// Ray from @p o along @p d over `(tmin, tmax)`.
rt::Ray make_ray(rt::vec3 o, rt::vec3 d, float tmin = 0.0f, float tmax = kInf) {
    return {o, tmin, d, tmax};
}

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

/// Non-owning view of @p bvh.
rt::BvhView view(const rt::Bvh& bvh) {
    return {bvh.nodes.empty() ? nullptr : bvh.nodes.data(),
            bvh.prim_refs.empty() ? nullptr : bvh.prim_refs.data(),
            static_cast<uint32_t>(bvh.nodes.size())};
}

/// Median BVH over @p s with the given leaf size, checked by the validator.
rt::Bvh build_median(const rt::Scene& s, uint32_t max_leaf_size) {
    rt::BvhParams p;
    p.method = rt::BvhMethod::Median;
    p.max_leaf_size = max_leaf_size;
    p.threads = 1;
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(s, p, timings);
    std::string err;
    EXPECT_TRUE(rt::validate_bvh(bvh, s, &err)) << err;
    return bvh;
}

/// Expects two Hits to be identical byte for byte (so -0 and +0, or two NaNs, also differ).
void expect_identical(const rt::Hit& a, const rt::Hit& b) {
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(rt::Hit)), 0)
        << "got {" << a.t << ", " << std::hex << a.prim << std::dec << ", " << a.u << ", " << a.v
        << "}, expected {" << b.t << ", " << std::hex << b.prim << std::dec << ", " << b.u << ", "
        << b.v << "}";
}

/// Uniform float in `[lo, hi)`.
float uniform(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

/// Uniform point in the box `[-h, h]^3`.
rt::vec3 uniform_vec(std::mt19937& rng, float h) {
    return {uniform(rng, -h, h), uniform(rng, -h, h), uniform(rng, -h, h)};
}

/// Bounds of sphere @p s.
rt::AABB sphere_box(const rt::Sphere& s) {
    const rt::vec3 r{s.radius, s.radius, s.radius};
    return {s.center - r, s.center + r};
}

const rt::vec3 kUp{0.0f, 0.0f, 1.0f};

// ---------------------------------------------------------------------------------------------
// Examples
// ---------------------------------------------------------------------------------------------

/// An empty BVH (null arrays, num_nodes == 0) is a miss at tmax and never occludes.
TEST(Traverse, EmptyBvh) {
    const rt::Scene s{};
    const rt::BvhView empty{nullptr, nullptr, 0};
    expect_identical(rt::traverse_closest(make_ray({0, 0, 0}, kUp), s, empty),
                     {kInf, rt::RT_MISS_PRIM, 0.0f, 0.0f});
    expect_identical(rt::traverse_closest(make_ray({0, 0, 0}, kUp, 0.0f, 7.0f), s, empty),
                     {7.0f, rt::RT_MISS_PRIM, 0.0f, 0.0f});
    EXPECT_FALSE(rt::traverse_any(make_ray({0, 0, 0}, kUp), s, empty));
}

/// A root leaf with one sphere: hit, miss, and a hit beyond tmax.
TEST(Traverse, SingleLeaf) {
    const std::vector<rt::Sphere> spheres{{{0.0f, 0.0f, 0.0f}, 1.0f, 0}};
    const rt::Scene s = make_scene(spheres, {});
    const rt::Bvh bvh = build_median(s, 4);
    ASSERT_EQ(bvh.nodes.size(), 1u);
    const rt::Ray hit = make_ray({0, 0, -5}, kUp);
    const rt::Ray miss = make_ray({3, 0, -5}, kUp);
    const rt::Ray short_ray = make_ray({0, 0, -5}, kUp, 0.0f, 3.0f);
    expect_identical(rt::traverse_closest(hit, s, view(bvh)), {4.0f, 0u, 0.0f, 0.0f});
    expect_identical(rt::traverse_closest(miss, s, view(bvh)), rt::brute_closest(miss, s));
    expect_identical(rt::traverse_closest(short_ray, s, view(bvh)),
                     {3.0f, rt::RT_MISS_PRIM, 0.0f, 0.0f});
    EXPECT_TRUE(rt::traverse_any(hit, s, view(bvh)));
    EXPECT_FALSE(rt::traverse_any(miss, s, view(bvh)));
    EXPECT_FALSE(rt::traverse_any(short_ray, s, view(bvh)));
}

/// Two identical spheres in different leaves. The right leaf's box is enlarged toward the ray,
/// so it is entered first and finds PrimRef 1. The left leaf's entry distance equals that hit
/// distance, so the inclusive culling rule must still visit it, and PrimRef 0 wins the tie.
TEST(Traverse, TieAcrossLeavesVisitsBothAndPicksLowest) {
    const rt::Sphere sp{{0.0f, 0.0f, 0.0f}, 1.0f, 0};
    const std::vector<rt::Sphere> spheres{sp, sp};
    const rt::Scene s = make_scene(spheres, {});
    const rt::AABB tight = sphere_box(sp);  // entered at t = 4 from z = -5
    const rt::AABB loose{{-1.0f, -1.0f, -2.0f}, {1.0f, 1.0f, 1.0f}};
    const std::vector<rt::BvhNode> nodes{{loose, 1u, 0u}, {tight, 0u, 1u}, {loose, 1u, 1u}};
    const std::vector<rt::PrimRef> refs{0u, 1u};
    const rt::BvhView bvh{nodes.data(), refs.data(), static_cast<uint32_t>(nodes.size())};
    const rt::Ray r = make_ray({0, 0, -5}, kUp);
    expect_identical(rt::traverse_closest(r, s, bvh), {4.0f, 0u, 0.0f, 0.0f});
    expect_identical(rt::traverse_closest(r, s, bvh), rt::brute_closest(r, s));
}

/// Same as above with the leaves swapped: the lower PrimRef is in the nearer box.
TEST(Traverse, TieAcrossLeavesNearerFirst) {
    const rt::Sphere sp{{0.0f, 0.0f, 0.0f}, 1.0f, 0};
    const std::vector<rt::Sphere> spheres{sp, sp};
    const rt::Scene s = make_scene(spheres, {});
    const rt::AABB tight = sphere_box(sp);
    const rt::AABB loose{{-1.0f, -1.0f, -2.0f}, {1.0f, 1.0f, 1.0f}};
    const std::vector<rt::BvhNode> nodes{{loose, 1u, 0u}, {tight, 1u, 1u}, {loose, 0u, 1u}};
    const std::vector<rt::PrimRef> refs{0u, 1u};
    const rt::BvhView bvh{nodes.data(), refs.data(), static_cast<uint32_t>(nodes.size())};
    const rt::Ray r = make_ray({0, 0, -5}, kUp);
    expect_identical(rt::traverse_closest(r, s, bvh), {4.0f, 0u, 0.0f, 0.0f});
}

/// A hit found in a nearer subtree culls a farther one: the far sphere's leaf is skipped and
/// the answer is unchanged.
TEST(Traverse, NearerHitWins) {
    const std::vector<rt::Sphere> spheres{{{0.0f, 0.0f, 10.0f}, 1.0f, 0},
                                          {{0.0f, 0.0f, 0.0f}, 1.0f, 0}};
    const rt::Scene s = make_scene(spheres, {});
    const rt::Bvh bvh = build_median(s, 1);
    const rt::Ray r = make_ray({0, 0, -5}, kUp);
    expect_identical(rt::traverse_closest(r, s, view(bvh)), {4.0f, 1u, 0.0f, 0.0f});
    // From the other side, sphere 0 is nearer.
    const rt::Ray back = make_ray({0, 0, 15}, {0, 0, -1});
    expect_identical(rt::traverse_closest(back, s, view(bvh)), {4.0f, 0u, 0.0f, 0.0f});
}

/// A shared edge between two triangles placed in different leaves resolves to the lower
/// PrimRef, as in brute force.
TEST(Traverse, SharedEdgeTieAcrossLeaves) {
    const std::vector<rt::Triangle> tris{
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0},
        {{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0}};
    const rt::Scene s = make_scene({}, tris);
    const rt::Bvh bvh = build_median(s, 1);
    ASSERT_GT(bvh.nodes.size(), 1u);
    const rt::Ray r = make_ray({0.5f, 0.5f, -1.0f}, kUp);
    expect_identical(rt::traverse_closest(r, s, view(bvh)),
                     {1.0f, rt::PRIM_TRIANGLE_BIT, 0.5f, 0.5f});
    expect_identical(rt::traverse_closest(r, s, view(bvh)), rt::brute_closest(r, s));
}

/// BvhQuery forwards to traverse_closest and traverse_any.
TEST(Traverse, QueryForwards) {
    const std::vector<rt::Sphere> spheres{{{0.0f, 0.0f, 0.0f}, 1.0f, 0},
                                          {{3.0f, 0.0f, 0.0f}, 1.0f, 0}};
    const rt::Scene s = make_scene(spheres, {});
    const rt::Bvh bvh = build_median(s, 1);
    const rt::BvhQuery q{s, view(bvh)};
    for (const rt::Ray& r :
         {make_ray({0, 0, -5}, kUp), make_ray({3, 0, -5}, kUp), make_ray({9, 9, -5}, kUp)}) {
        expect_identical(q.closest(r), rt::traverse_closest(r, s, view(bvh)));
        EXPECT_EQ(q.any(r), rt::traverse_any(r, s, view(bvh)));
    }
}

/// BvhQuery holds only the scene and BVH views, so passing it to a kernel is cheap.
TEST(Traverse, QueryLayout) {
    EXPECT_EQ(sizeof(rt::BvhQuery), sizeof(rt::Scene) + sizeof(rt::BvhView));
}

// ---------------------------------------------------------------------------------------------
// Stack overflow
// ---------------------------------------------------------------------------------------------

/// Hand-built chain of @p levels interior nodes, every box equal to the unit sphere's. Each
/// interior node's left child is the next interior node and its right child is a leaf holding
/// sphere 0. Both children are always hit at the same `t_enter`, so traversal descends left and
/// pushes the leaf once per level: @p levels pushes in total.
struct Chain {
    std::vector<rt::BvhNode> nodes;
    std::vector<rt::PrimRef> refs{0u};
    rt::BvhView view() const { return {nodes.data(), refs.data(), uint32_t(nodes.size())}; }
};

Chain make_chain(int levels) {
    const rt::AABB box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    Chain c;
    c.nodes.push_back({box, 0u, 0u});
    uint32_t cur = 0;
    for (int k = 0; k < levels; ++k) {
        const uint32_t left = static_cast<uint32_t>(c.nodes.size());
        c.nodes[cur].left_or_first = left;
        c.nodes.push_back({box, 0u, 0u});  // next interior node (or the last leaf)
        c.nodes.push_back({box, 0u, 1u});  // leaf
        cur = left;
    }
    c.nodes[cur].count = 1u;  // deepest node is a leaf, at depth `levels`
    return c;
}

const std::vector<rt::Sphere> kChainSpheres{{{0.0f, 0.0f, 0.0f}, 1.0f, 0}};

/// A tree of depth RT_MAX_BVH_DEPTH fills the 64-entry stack exactly without overflowing.
TEST(TraverseOverflow, DepthLimitFits) {
    const Chain c = make_chain(rt::RT_MAX_BVH_DEPTH);
    const rt::Scene s = make_scene(kChainSpheres, {});
    const rt::Ray r = make_ray({0, 0, -5}, kUp);
    uint32_t flag = 0;
    expect_identical(rt::traverse_closest(r, s, c.view(), &flag), {4.0f, 0u, 0.0f, 0.0f});
    EXPECT_TRUE(rt::traverse_any(r, s, c.view(), &flag));
    EXPECT_EQ(flag, 0u);
}

#ifdef NDEBUG
/// Release builds: one level deeper sets the flag and stops the ray.
TEST(TraverseOverflow, TooDeepSetsFlag) {
    const Chain c = make_chain(rt::RT_MAX_BVH_DEPTH + 1);
    const rt::Scene s = make_scene(kChainSpheres, {});
    const rt::Ray r = make_ray({0, 0, -5}, kUp);
    uint32_t flag = 0;
    rt::traverse_closest(r, s, c.view(), &flag);
    EXPECT_EQ(flag, 1u);
    flag = 0;
    EXPECT_FALSE(rt::traverse_any(r, s, c.view(), &flag));
    EXPECT_EQ(flag, 1u);
    // Without a flag pointer the ray still stops instead of writing past the stack.
    rt::traverse_closest(r, s, c.view());
}
#else
/// Debug builds assert on overflow.
TEST(TraverseOverflowDeathTest, TooDeepAsserts) {
    const Chain c = make_chain(rt::RT_MAX_BVH_DEPTH + 1);
    const rt::Scene s = make_scene(kChainSpheres, {});
    const rt::Ray r = make_ray({0, 0, -5}, kUp);
    EXPECT_DEATH(rt::traverse_closest(r, s, c.view()), "stack overflow");
}
#endif

// ---------------------------------------------------------------------------------------------
// Properties: BVH = brute force, bit for bit
// ---------------------------------------------------------------------------------------------

/// Random non-degenerate triangle with vertices within @p h of @p c.
rt::Triangle random_triangle(std::mt19937& rng, rt::vec3 c, float h) {
    for (;;) {
        const rt::Triangle tri{c + uniform_vec(rng, h), c + uniform_vec(rng, h),
                               c + uniform_vec(rng, h), 0};
        if (rt::length(rt::cross(tri.v1 - tri.v0, tri.v2 - tri.v0)) > 1.0e-2f) return tri;
    }
}

/// Random scene of up to @p max_prims spheres and triangles in `[-20, 20]^3`. About one in six
/// primitives duplicates an earlier one, and a few triangles are degenerate, so exact ties and
/// zero-area primitives occur.
void random_scene(std::mt19937& rng, uint32_t max_prims, std::vector<rt::Sphere>& spheres,
                  std::vector<rt::Triangle>& tris) {
    spheres.clear();
    tris.clear();
    const uint32_t ns = rng() % (max_prims + 1);
    const uint32_t nt = rng() % (max_prims + 1);
    for (uint32_t i = 0; i < ns; ++i) {
        if (i > 0 && rng() % 6u == 0) {
            spheres.push_back(spheres[rng() % spheres.size()]);
        } else {
            spheres.push_back({uniform_vec(rng, 20.0f), uniform(rng, 0.2f, 3.0f), 0});
        }
    }
    for (uint32_t i = 0; i < nt; ++i) {
        const uint32_t kind = rng() % 16u;
        if (i > 0 && kind < 3) {
            tris.push_back(tris[rng() % tris.size()]);
        } else if (kind == 3) {
            const rt::vec3 a = uniform_vec(rng, 20.0f);
            tris.push_back({a, a + rt::vec3{1, 1, 1}, a + rt::vec3{2, 2, 2}, 0});  // collinear
        } else {
            tris.push_back(random_triangle(rng, uniform_vec(rng, 20.0f), 3.0f));
        }
    }
}

/// Random ray: usually aimed near a primitive, sometimes in a random direction, with tmin in
/// {0, 1e-3} and tmax +inf or finite.
rt::Ray random_ray(std::mt19937& rng, const std::vector<rt::Sphere>& spheres,
                   const std::vector<rt::Triangle>& tris) {
    const rt::vec3 o = uniform_vec(rng, 40.0f);
    rt::vec3 aim = uniform_vec(rng, 20.0f);
    const size_t n = spheres.size() + tris.size();
    if (n > 0 && rng() % 4u != 0) {
        const size_t k = rng() % n;
        if (k < spheres.size()) {
            aim = spheres[k].center;
        } else {
            const rt::Triangle& t = tris[k - spheres.size()];
            aim = (t.v0 + t.v1 + t.v2) / 3.0f;
        }
        aim = aim + uniform_vec(rng, 1.5f);
    }
    rt::vec3 d = aim - o;
    if (rt::length(d) < 1.0e-3f) d = kUp;
    const float tmin = rng() % 2u ? 0.0f : 1.0e-3f;
    const float tmax = rng() % 3u ? kInf : uniform(rng, 0.2f, 1.2f);
    return make_ray(o, d, tmin, tmax);
}

/// Feature: bvh-traversal, Property 1: on random scenes and rays, traverse_closest returns the
/// same Hit as brute_closest byte for byte, and traverse_any matches brute_any, for every leaf
/// size.
TEST(TraverseProperty, MatchesBruteForce) {
    std::mt19937 rng(201);
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> tris;
    int hits = 0, rays = 0;
    for (int scene = 0; scene < 200; ++scene) {
        random_scene(rng, 150, spheres, tris);
        const rt::Scene s = make_scene(spheres, tris);
        for (uint32_t leaf : {1u, 2u, 4u, 8u}) {
            const rt::Bvh bvh = build_median(s, leaf);
            const rt::BvhView v = view(bvh);
            for (int k = 0; k < 50; ++k) {
                const rt::Ray r = random_ray(rng, spheres, tris);
                SCOPED_TRACE("seed 201 scene " + std::to_string(scene) + " leaf " +
                             std::to_string(leaf) + " ray " + std::to_string(k));
                uint32_t flag = 0;
                const rt::Hit expect = rt::brute_closest(r, s);
                expect_identical(rt::traverse_closest(r, s, v, &flag), expect);
                EXPECT_EQ(rt::traverse_any(r, s, v, &flag), rt::brute_any(r, s));
                EXPECT_EQ(flag, 0u);
                hits += expect.prim != rt::RT_MISS_PRIM;
                ++rays;
            }
        }
    }
    EXPECT_GT(hits, rays / 4);  // the rays exercise hits, not only misses
}

/// Feature: bvh-traversal, Property 2: on a quad mesh with shared edges and vertices, rays
/// through exact grid vertices and edge midpoints (many exact ties across leaves) match brute
/// force byte for byte, and never pass between triangles.
TEST(TraverseProperty, GridTiesMatchBruteForce) {
    constexpr int kN = 16;  // kN x kN quads, two triangles each, in the z = 0 plane
    std::vector<rt::Triangle> tris;
    for (int y = 0; y < kN; ++y) {
        for (int x = 0; x < kN; ++x) {
            const rt::vec3 a{float(x), float(y), 0.0f}, b{float(x + 1), float(y), 0.0f};
            const rt::vec3 c{float(x + 1), float(y + 1), 0.0f}, d{float(x), float(y + 1), 0.0f};
            tris.push_back({a, b, c, 0});
            tris.push_back({a, c, d, 0});
        }
    }
    // Spheres resting on the grid, tangent to it at a vertex, add sphere/triangle ties.
    std::vector<rt::Sphere> spheres;
    for (int k = 0; k < 8; ++k) {
        spheres.push_back({{float(2 * k + 1), float(2 * k + 1), -1.0f}, 1.0f, 0});
    }
    const rt::Scene s = make_scene(spheres, tris);
    std::mt19937 rng(202);
    for (uint32_t leaf : {1u, 4u}) {
        const rt::Bvh bvh = build_median(s, leaf);
        for (int i = 0; i <= 2 * kN; ++i) {
            for (int j = 0; j <= 2 * kN; ++j) {
                const rt::vec3 target{0.5f * float(i), 0.5f * float(j), 0.0f};
                // Straight down, and from a random dyadic offset (exact float directions).
                const rt::vec3 off{float(int(rng() % 9u) - 4) * 0.25f,
                                   float(int(rng() % 9u) - 4) * 0.25f, 4.0f};
                for (const rt::Ray& r : {make_ray(target + rt::vec3{0, 0, 4}, {0, 0, -1}),
                                         make_ray(target + off, -1.0f * off)}) {
                    SCOPED_TRACE("leaf " + std::to_string(leaf) + " target " + std::to_string(i) +
                                 "," + std::to_string(j));
                    const rt::Hit h = rt::traverse_closest(r, s, view(bvh));
                    expect_identical(h, rt::brute_closest(r, s));
                    EXPECT_NE(h.prim, rt::RT_MISS_PRIM);
                    EXPECT_TRUE(rt::traverse_any(r, s, view(bvh)));
                }
            }
        }
    }
}

/// Feature: bvh-traversal, Property 3: when every centroid is equal (identical spheres), the
/// BVH splits by canonical order and traversal still returns the lowest PrimRef.
TEST(TraverseProperty, AllEqualCentroids) {
    const std::vector<rt::Sphere> spheres(37, rt::Sphere{{1.0f, 2.0f, 3.0f}, 1.5f, 0});
    const rt::Scene s = make_scene(spheres, {});
    std::mt19937 rng(203);
    for (uint32_t leaf : {1u, 3u}) {
        const rt::Bvh bvh = build_median(s, leaf);
        for (int k = 0; k < 200; ++k) {
            const rt::vec3 o = uniform_vec(rng, 20.0f);
            const rt::Ray r = make_ray(o, rt::vec3{1, 2, 3} + uniform_vec(rng, 1.0f) - o);
            const rt::Hit h = rt::traverse_closest(r, s, view(bvh));
            expect_identical(h, rt::brute_closest(r, s));
            if (h.prim != rt::RT_MISS_PRIM) {
                EXPECT_EQ(h.prim, 0u);
            }
        }
    }
}

/// Feature: bvh-traversal, Property 4: axis-aligned rays (zero direction components, both
/// signs of zero) and rays starting inside the scene bounds match brute force byte for byte.
TEST(TraverseProperty, AxisAlignedAndInteriorRays) {
    std::mt19937 rng(204);
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> tris;
    for (int scene = 0; scene < 50; ++scene) {
        random_scene(rng, 100, spheres, tris);
        const rt::Scene s = make_scene(spheres, tris);
        const rt::Bvh bvh = build_median(s, 4);
        for (int k = 0; k < 60; ++k) {
            const rt::vec3 o = uniform_vec(rng, 20.0f);
            rt::vec3 d{0.0f, 0.0f, 0.0f};
            const int axis = static_cast<int>(rng() % 3u);
            const float sign = rng() % 2u ? 1.0f : -1.0f;
            if (axis == 0) d = {sign, rng() % 2u ? 0.0f : -0.0f, rng() % 2u ? 0.0f : -0.0f};
            if (axis == 1) d = {rng() % 2u ? 0.0f : -0.0f, sign, rng() % 2u ? 0.0f : -0.0f};
            if (axis == 2) d = {rng() % 2u ? 0.0f : -0.0f, rng() % 2u ? 0.0f : -0.0f, sign};
            const rt::Ray r = make_ray(o, d);
            SCOPED_TRACE("seed 204 scene " + std::to_string(scene) + " ray " + std::to_string(k));
            expect_identical(rt::traverse_closest(r, s, view(bvh)), rt::brute_closest(r, s));
            EXPECT_EQ(rt::traverse_any(r, s, view(bvh)), rt::brute_any(r, s));
        }
    }
}

}  // namespace

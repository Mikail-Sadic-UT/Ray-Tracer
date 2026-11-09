/// @file test_bvh.cpp
/// @brief Bounds and deterministic median-builder checks.
#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <vector>

#include "bvh/bvh.hpp"
#include "core/error.hpp"

namespace {

rt::Scene make_scene(const std::vector<rt::Sphere>& spheres,
                     const std::vector<rt::Triangle>& triangles) {
    return {spheres.data(),
            static_cast<uint32_t>(spheres.size()),
            triangles.data(),
            static_cast<uint32_t>(triangles.size()),
            nullptr,
            0,
            nullptr,
            0,
            {},
            0.0f};
}

/// Fixed-seed mix of spheres and triangles in a 100-unit cube.
void random_primitives(uint32_t n, std::vector<rt::Sphere>& spheres,
                       std::vector<rt::Triangle>& triangles) {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> pos(-50.0f, 50.0f);
    std::uniform_real_distribution<float> size(0.1f, 2.0f);
    for (uint32_t i = 0; i < n; ++i) {
        const rt::vec3 c{pos(rng), pos(rng), pos(rng)};
        if (i % 2 == 0) {
            spheres.push_back({c, size(rng), 0});
        } else {
            const float s = size(rng);
            triangles.push_back({c, c + rt::vec3{s, 0.0f, 0.0f}, c + rt::vec3{0.0f, s, s}, 0});
        }
    }
}

TEST(Bvh, EmptyScene) {
    rt::Timings timings;
    const rt::Scene scene = make_scene({}, {});
    const rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    EXPECT_TRUE(bvh.nodes.empty());
    EXPECT_TRUE(bvh.prim_refs.empty());
    EXPECT_TRUE(rt::validate_bvh(bvh, scene));
    EXPECT_DOUBLE_EQ(bvh.stats.sah_cost, 0.0);
}

TEST(Bvh, OnePrimitiveIsAValidatedLeaf) {
    const std::vector<rt::Sphere> spheres{{{1.0f, -2.0f, 3.0f}, 2.0f, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::BvhParams params;
    params.max_leaf_size = 1;
    const rt::Bvh bvh = rt::build_bvh(scene, params, timings);

    ASSERT_EQ(bvh.nodes.size(), 1u);
    ASSERT_EQ(bvh.prim_refs.size(), 1u);
    EXPECT_EQ(bvh.nodes[0].count, 1u);
    EXPECT_EQ(bvh.prim_refs[0], 0u);
    EXPECT_EQ(bvh.stats.nodes, 1u);
    EXPECT_EQ(bvh.stats.leaves, 1u);
    EXPECT_EQ(bvh.stats.depth, 0u);
    EXPECT_EQ(bvh.stats.max_leaf_size, 1u);
    EXPECT_GT(bvh.stats.sah_cost, 0.0);
    EXPECT_TRUE(rt::validate_bvh(bvh, scene));
}

TEST(Bvh, MedianBuildCoversCanonicalSphereThenTriangleOrder) {
    const std::vector<rt::Sphere> spheres{{{0.0f, 0.0f, 0.0f}, 2.0f, 0}};
    const std::vector<rt::Triangle> triangles{
        {{3.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}, 0}};
    const rt::Scene scene = make_scene(spheres, triangles);
    rt::Timings timings;
    rt::BvhParams params;
    params.max_leaf_size = 1;
    const rt::Bvh bvh = rt::build_bvh(scene, params, timings);
    ASSERT_EQ(bvh.nodes.size(), 3u);
    ASSERT_EQ(bvh.prim_refs.size(), 2u);
    EXPECT_EQ(bvh.prim_refs[0], 0u);
    EXPECT_EQ(bvh.prim_refs[1], rt::PRIM_TRIANGLE_BIT);
    EXPECT_TRUE(rt::validate_bvh(bvh, scene));
    // Sphere bounds are rounded outward, so the root reaches at least -2 and exactly 4.
    EXPECT_LE(bvh.nodes[0].bounds.lo.x, -2.0f);
    EXPECT_FLOAT_EQ(bvh.nodes[0].bounds.hi.x, 4.0f);
}

TEST(Bvh, TwoPrimitivesFitOneDefaultLeaf) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}, {{5, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    const rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    ASSERT_EQ(bvh.nodes.size(), 1u);
    EXPECT_EQ(bvh.nodes[0].count, 2u);
    EXPECT_TRUE(rt::validate_bvh(bvh, scene));
}

TEST(Bvh, SphereBoundsContainTheExactSphere) {
    // 0.1 is not exact in float, so center - radius rounds; the box must still enclose it.
    const std::vector<rt::Sphere> spheres{{{0.1f, 1000.3f, -7.7f}, 0.3f, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    const rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    const rt::AABB& b = bvh.nodes[0].bounds;
    for (int a = 0; a < 3; ++a) {
        const double c = spheres[0].center[a];
        const double r = spheres[0].radius;
        EXPECT_LE(static_cast<double>(b.lo[a]), c - r) << "axis " << a;
        EXPECT_GE(static_cast<double>(b.hi[a]), c + r) << "axis " << a;
    }
}

TEST(Bvh, EqualCentroidsUsePrimitiveReferenceTieBreak) {
    const std::vector<rt::Sphere> spheres(5, rt::Sphere{{0, 0, 0}, 1, 0});
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::BvhParams params;
    params.max_leaf_size = 1;
    const rt::Bvh bvh = rt::build_bvh(scene, params, timings);
    EXPECT_EQ(bvh.prim_refs, (std::vector<rt::PrimRef>{0, 1, 2, 3, 4}));
    EXPECT_TRUE(rt::validate_bvh(bvh, scene));
}

TEST(Bvh, RandomSceneValidatesAndIsDeterministic) {
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> triangles;
    random_primitives(1000, spheres, triangles);
    const rt::Scene scene = make_scene(spheres, triangles);
    rt::Timings timings;
    for (uint32_t leaf : {1u, 4u, 8u}) {
        rt::BvhParams params;
        params.max_leaf_size = leaf;
        const rt::Bvh first = rt::build_bvh(scene, params, timings);
        const rt::Bvh second = rt::build_bvh(scene, params, timings);
        std::string error;
        EXPECT_TRUE(rt::validate_bvh(first, scene, &error)) << "leaf " << leaf << ": " << error;
        EXPECT_EQ(first.prim_refs, second.prim_refs);
        ASSERT_EQ(first.nodes.size(), second.nodes.size());
        EXPECT_EQ(0, std::memcmp(first.nodes.data(), second.nodes.data(),
                                 first.nodes.size() * sizeof(rt::BvhNode)));
        // Median halves the count, so depth <= ceil(log2(1000 / leaf)) + 1.
        EXPECT_LE(first.stats.depth, 10u);
        EXPECT_EQ(first.stats.fallbacks, 0u);
    }
}

TEST(Bvh, ValidatorRejectsUnreachableNodes) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    bvh.nodes.push_back(bvh.nodes.front());

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: unreachable node");
}

TEST(Bvh, ValidatorRejectsInvalidPrimitiveReference) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    bvh.prim_refs[0] = 1u;

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: invalid primitive reference");
}

TEST(Bvh, LbvhIsValidAndDeterministicAcrossThreadCounts) {
    rt::Sphere spheres[17];
    for (uint32_t i = 0; i < 17; ++i) {
        spheres[i] = {{static_cast<float>(i % 5) * 2.0f, static_cast<float>(i / 5),
                       static_cast<float>(i % 3) * -1.5f},
                      0.25f + static_cast<float>(i % 2) * 0.1f,
                      0};
    }
    const rt::Scene scene{spheres, 17, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};

    rt::BvhParams one_thread;
    one_thread.method = rt::BvhMethod::LBVH;
    one_thread.max_leaf_size = 2;
    one_thread.threads = 1;
    rt::BvhParams many_threads = one_thread;
    many_threads.threads = 8;
    rt::BvhParams seventy_two_threads = one_thread;
    seventy_two_threads.threads = 72;

    rt::Timings one_timings, many_timings, seventy_two_timings;
    const rt::Bvh one = rt::build_bvh(scene, one_thread, one_timings);
    const rt::Bvh many = rt::build_bvh(scene, many_threads, many_timings);
    const rt::Bvh seventy_two = rt::build_bvh(scene, seventy_two_threads, seventy_two_timings);

    ASSERT_TRUE(rt::validate_bvh(one, scene, nullptr));
    ASSERT_TRUE(rt::validate_bvh(many, scene, nullptr));
    ASSERT_TRUE(rt::validate_bvh(seventy_two, scene, nullptr));
    ASSERT_EQ(one.prim_refs, many.prim_refs);
    ASSERT_EQ(one.prim_refs, seventy_two.prim_refs);
    ASSERT_EQ(one.nodes.size(), many.nodes.size());
    ASSERT_EQ(one.nodes.size(), seventy_two.nodes.size());
    EXPECT_EQ(
        std::memcmp(one.nodes.data(), many.nodes.data(), one.nodes.size() * sizeof(rt::BvhNode)),
        0);
    EXPECT_EQ(std::memcmp(one.nodes.data(), seventy_two.nodes.data(),
                          one.nodes.size() * sizeof(rt::BvhNode)),
              0);
    EXPECT_EQ(one.stats.nodes, many.stats.nodes);
    EXPECT_EQ(one.stats.nodes, seventy_two.stats.nodes);
    EXPECT_EQ(one.stats.leaves, many.stats.leaves);
    EXPECT_EQ(one.stats.leaves, seventy_two.stats.leaves);
    EXPECT_EQ(one.stats.depth, many.stats.depth);
    EXPECT_EQ(one.stats.depth, seventy_two.stats.depth);
}

TEST(Bvh, LbvhUsesCanonicalOrderForEqualCentroids) {
    rt::Sphere spheres[4] = {
        {{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}};
    const rt::Scene scene{spheres, 4, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
    rt::BvhParams params;
    params.method = rt::BvhMethod::LBVH;
    params.max_leaf_size = 1;
    params.threads = 4;

    rt::Timings timings;
    const rt::Bvh bvh = rt::build_bvh(scene, params, timings);
    ASSERT_TRUE(rt::validate_bvh(bvh, scene, nullptr));
    ASSERT_EQ(bvh.prim_refs.size(), 4u);
    EXPECT_EQ(bvh.prim_refs[0], 0u);
    EXPECT_EQ(bvh.prim_refs[1], 1u);
    EXPECT_EQ(bvh.prim_refs[2], 2u);
    EXPECT_EQ(bvh.prim_refs[3], 3u);
}

/// Two spheres split into two leaves, the smallest tree with an interior node.
rt::Bvh two_leaf_bvh(const rt::Scene& scene) {
    rt::Timings timings;
    rt::BvhParams params;
    params.max_leaf_size = 1;
    return rt::build_bvh(scene, params, timings);
}

TEST(Bvh, ValidatorRejectsDuplicatePrimitive) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Bvh bvh = two_leaf_bvh(scene);
    bvh.prim_refs[1] = bvh.prim_refs[0];

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: duplicate primitive");
}

TEST(Bvh, ValidatorRejectsChildEscapingParent) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}, {{5, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Bvh bvh = two_leaf_bvh(scene);
    bvh.nodes[0].bounds.hi.x = 3.0f;

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: child escapes parent bounds");
}

TEST(Bvh, ValidatorRejectsPrimitiveEscapingLeaf) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    bvh.nodes[0].bounds.hi.x = 0.5f;

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: primitive escapes leaf bounds");
}

TEST(Bvh, ValidatorRejectsOversizedLeaf) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}, {{5, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(scene, {}, timings);  // one leaf of 2
    bvh.stats.max_leaf_size = 1;

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: leaf exceeds max_leaf_size");
}

TEST(Bvh, ValidatorRejectsDepthOver64) {
    // A chain of 66 nodes reaching depth 65; only the deepest leaf holds the primitive.
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    const rt::BvhNode leaf = bvh.nodes[0];
    bvh.nodes.clear();
    for (uint32_t d = 0; d < 65; ++d) {
        const uint32_t left = static_cast<uint32_t>(bvh.nodes.size()) + (d == 0 ? 1 : 2);
        bvh.nodes.push_back({leaf.bounds, left, 0});
        if (d > 0) bvh.nodes.push_back({leaf.bounds, 0, 1});  // right sibling leaf
    }
    bvh.nodes.push_back(leaf);
    bvh.nodes.push_back({leaf.bounds, 0, 1});

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: depth exceeds 64");
}

TEST(Bvh, RejectsInvalidParameters) {
    const rt::Scene scene = make_scene({}, {});
    rt::Timings timings;
    rt::BvhParams params;
    params.method = rt::BvhMethod::SAH;
    EXPECT_THROW(rt::build_bvh(scene, params, timings), rt::Error);
    params = {};
    params.max_leaf_size = 0;
    EXPECT_THROW(rt::build_bvh(scene, params, timings), rt::Error);
    params = {};
    params.threads = 0;
    EXPECT_THROW(rt::build_bvh(scene, params, timings), rt::Error);
}

}  // namespace

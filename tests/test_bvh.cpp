/// @file test_bvh.cpp
/// @brief Bounds and deterministic median-builder checks.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#include "bvh/bvh.hpp"
#include "bvh/lbvh_internal.hpp"
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

TEST(Bvh, SahCostMatchesAnalyticLeafAreasAtDifferentScales) {
    // Two boxes of area 24 and a root of area 56: leaf SAH = 48/56 = 6/7.
    // Large coordinates are valid scene data, but their float surface areas overflow:
    // at scale 1e19 the root area is 56e38, above FLT_MAX (about 3.4e38).
    for (float scale : {1.0f, 1.0e19f}) {
        const std::vector<rt::Triangle> triangles{
            {{0, 0, 0}, {2 * scale, 0, 2 * scale}, {0, 2 * scale, 0}, 0},
            {{4 * scale, 0, 0}, {6 * scale, 0, 2 * scale}, {4 * scale, 2 * scale, 0}, 0}};
        const rt::Scene scene = make_scene({}, triangles);
        rt::BvhParams params;
        params.max_leaf_size = 1;
        rt::Timings timings;
        const rt::Bvh tree = rt::build_bvh(scene, params, timings);
        ASSERT_TRUE(rt::validate_bvh(tree, scene));
        EXPECT_NEAR(tree.stats.sah_cost, 6.0 / 7.0, 1e-6);
    }
}

TEST(Bvh, ZeroAreaTriangleIsRetainedWithZeroSahCost) {
    const std::vector<rt::Triangle> triangles{{{1, 2, 3}, {1, 2, 3}, {1, 2, 3}, 0}};
    const rt::Scene scene = make_scene({}, triangles);
    rt::Timings timings;
    const rt::Bvh tree = rt::build_bvh(scene, {}, timings);
    EXPECT_TRUE(rt::validate_bvh(tree, scene));
    ASSERT_EQ(tree.prim_refs.size(), 1u);
    EXPECT_EQ(tree.prim_refs[0], rt::PRIM_TRIANGLE_BIT);
    EXPECT_DOUBLE_EQ(tree.stats.sah_cost, 0.0);
}

TEST(Bvh, ValidatorRejectsNonfiniteRootLeafBounds) {
    const std::vector<rt::Sphere> spheres{{{0, 0, 0}, 1, 0}};
    const rt::Scene scene = make_scene(spheres, {});
    rt::Timings timings;
    const rt::Bvh original = rt::build_bvh(scene, {}, timings);
    for (int axis = 0; axis < 3; ++axis) {
        for (float value :
             {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
              std::numeric_limits<float>::quiet_NaN()}) {
            for (bool lower : {false, true}) {
                rt::Bvh tree = original;
                (lower ? tree.nodes[0].bounds.lo : tree.nodes[0].bounds.hi)[axis] = value;
                std::string error;
                EXPECT_FALSE(rt::validate_bvh(tree, scene, &error));
                EXPECT_EQ(error, "bvh: node has nonfinite bounds");
            }
        }
    }
    std::string error = "previous failure";
    EXPECT_TRUE(rt::validate_bvh(original, scene, &error));
    EXPECT_TRUE(error.empty());
}

TEST(Bvh, ClusteredSceneIsByteIdenticalForOneEightAndSeventyTwoThreads) {
    // The serial median builder ignores params.threads, so today this guards the
    // contract for the task-parallel median variant (INTERFACES §3) once it lands.
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> triangles;
    random_primitives(2048, spheres, triangles);
    // Compress geometry into two tight clusters without using the builder's bounds helpers.
    for (size_t i = 0; i < spheres.size(); ++i)
        spheres[i].center = spheres[i].center * 0.01f + rt::vec3{(i % 2) ? 20.0f : -20.0f, 0, 0};
    for (size_t i = 0; i < triangles.size(); ++i) {
        const rt::vec3 shift{(i % 2) ? 20.0f : -20.0f, 0, 0};
        triangles[i].v0 = triangles[i].v0 * 0.01f + shift;
        triangles[i].v1 = triangles[i].v1 * 0.01f + shift;
        triangles[i].v2 = triangles[i].v2 * 0.01f + shift;
    }
    const rt::Scene scene = make_scene(spheres, triangles);
    rt::Timings timings;
    for (uint32_t leaf_size : {1u, 4u, 8u}) {
        rt::BvhParams params;
        params.max_leaf_size = leaf_size;
        const rt::Bvh baseline = rt::build_bvh(scene, params, timings);
        ASSERT_TRUE(rt::validate_bvh(baseline, scene));
        for (int threads : {8, 72}) {
            params.threads = threads;
            const rt::Bvh tree = rt::build_bvh(scene, params, timings);
            ASSERT_TRUE(rt::validate_bvh(tree, scene));
            EXPECT_EQ(tree.prim_refs, baseline.prim_refs);
            ASSERT_EQ(tree.nodes.size(), baseline.nodes.size());
            EXPECT_EQ(std::memcmp(tree.nodes.data(), baseline.nodes.data(),
                                  tree.nodes.size() * sizeof(rt::BvhNode)),
                      0);
            EXPECT_EQ(tree.stats.depth, baseline.stats.depth);
            EXPECT_DOUBLE_EQ(tree.stats.sah_cost, baseline.stats.sah_cost);
        }
    }
}

TEST(Bvh, EmptyBuildClearsPreviousPreparationTimings) {
    rt::Timings timings;
    timings.bounds = timings.build = timings.layout = 123.0;
    timings.render = 456.0;
    const rt::Scene scene = make_scene({}, {});
    rt::build_bvh(scene, {}, timings);
    EXPECT_GE(timings.bounds, 0.0);
    EXPECT_DOUBLE_EQ(timings.build, 0.0);
    EXPECT_DOUBLE_EQ(timings.layout, 0.0);
    EXPECT_DOUBLE_EQ(timings.render, 456.0);
}

TEST(Bvh, LbvhTaskSizedScenesAreDeterministicWithoutFallbacks) {
    for (bool clustered : {false, true}) {
        std::vector<rt::Sphere> spheres;
        std::vector<rt::Triangle> triangles;
        random_primitives(4096, spheres, triangles);
        if (clustered) {
            for (size_t i = 0; i < spheres.size(); ++i)
                spheres[i].center =
                    spheres[i].center * 0.001f + rt::vec3{(i % 2) ? 20.0f : -20.0f, 0, 0};
            for (size_t i = 0; i < triangles.size(); ++i) {
                const rt::vec3 shift{(i % 2) ? 20.0f : -20.0f, 0, 0};
                triangles[i].v0 = triangles[i].v0 * 0.001f + shift;
                triangles[i].v1 = triangles[i].v1 * 0.001f + shift;
                triangles[i].v2 = triangles[i].v2 * 0.001f + shift;
            }
        }
        const rt::Scene scene = make_scene(spheres, triangles);
        rt::BvhParams params;
        params.method = rt::BvhMethod::LBVH;
        params.max_leaf_size = 1;
        rt::Timings timings;
        const rt::Bvh baseline = rt::build_bvh(scene, params, timings);
        ASSERT_TRUE(rt::validate_bvh(baseline, scene));
        ASSERT_EQ(baseline.stats.fallbacks, 0u);
        for (int threads : {8, 72}) {
            params.threads = threads;
            for (int repeat = 0; repeat < 2; ++repeat) {
                const rt::Bvh tree = rt::build_bvh(scene, params, timings);
                ASSERT_TRUE(rt::validate_bvh(tree, scene));
                EXPECT_EQ(tree.stats.fallbacks, 0u);
                EXPECT_EQ(tree.prim_refs, baseline.prim_refs);
                ASSERT_EQ(tree.nodes.size(), baseline.nodes.size());
                EXPECT_EQ(std::memcmp(tree.nodes.data(), baseline.nodes.data(),
                                      tree.nodes.size() * sizeof(rt::BvhNode)),
                          0);
            }
        }
    }
}

TEST(Bvh, LbvhMortonChainDeeperThan32DoesNotFallback) {
    std::vector<rt::Sphere> spheres(512, rt::Sphere{{0, 0, 0}, 0.00001f, 0});
    // Nested Morton bit prefixes followed by a large equal-code group: the full
    // tree exceeds depth 32 but fits the 64-entry traversal stack.
    for (int bit = 0; bit < 30; ++bit)
        spheres[static_cast<size_t>(bit)].center[bit % 3] = static_cast<float>(1u << (bit / 3));
    const rt::Scene scene = make_scene(spheres, {});
    rt::BvhParams params;
    params.method = rt::BvhMethod::LBVH;
    params.max_leaf_size = 1;
    rt::Timings timings;
    const rt::Bvh tree = rt::build_bvh(scene, params, timings);
    EXPECT_TRUE(rt::validate_bvh(tree, scene));
    EXPECT_GT(tree.stats.depth, 32u);
    EXPECT_LE(tree.stats.depth, 64u);
    EXPECT_EQ(tree.stats.fallbacks, 0u);
}

TEST(Bvh, LbvhRecoveryPreservesDepth64AndRepairsOnlyDepth32Subtree) {
    // 30-bit Morton/index hierarchies cannot reach depth >64 at practical input
    // sizes. Inject full binary chains into the actual internal recovery pass.
    for (uint32_t depth : {64u, 65u}) {
        const uint32_t count = depth + 1;
        std::vector<rt::detail::BuildPrim> prims(count);
        std::vector<uint32_t> order(count);
        std::iota(order.begin(), order.end(), 0u);
        std::vector<rt::detail::LbvhNode> nodes(2 * count - 1);
        for (uint32_t i = 0; i < count; ++i) {
            const float x = static_cast<float>(count - i);
            prims[i] = {i, {{x, 0, 0}, {x + 0.5f, 1, 1}}, {x + 0.25f, 0.5f, 0.5f}};
            if (i < depth) {
                auto& node = nodes[2 * i];
                node.first = i;
                node.last = count - 1;
                node.left = 2 * i + 1;
                node.right = 2 * i + 2;
                node.bounds = {{0, 0, 0}, {static_cast<float>(count + 1), 1, 1}};
                auto& leaf = nodes[2 * i + 1];
                leaf.first = leaf.last = i;
                leaf.leaf = true;
                leaf.bounds = prims[i].bounds;
            }
        }
        nodes.back().first = nodes.back().last = count - 1;
        nodes.back().leaf = true;
        nodes.back().bounds = prims.back().bounds;
        const auto original = nodes;
        const uint32_t fallbacks = rt::detail::recover_lbvh_depth(prims, order, nodes);
        EXPECT_EQ(fallbacks, depth == 64 ? 0u : 1u);
        EXPECT_LE(nodes[0].height, 64u);
        // Every ancestor keeps its topology and primitive range, and each outside
        // sibling leaf is untouched; only the depth-32 range may be permuted.
        for (uint32_t i = 0; i < 32; ++i) {
            EXPECT_EQ(nodes[2 * i].left, original[2 * i].left);
            EXPECT_EQ(nodes[2 * i].right, original[2 * i].right);
            EXPECT_EQ(nodes[2 * i].first, original[2 * i].first);
            EXPECT_EQ(nodes[2 * i].last, original[2 * i].last);
            EXPECT_EQ(nodes[2 * i + 1].first, i);
            EXPECT_EQ(order[i], i);
        }
        if (depth == 64) {
            for (uint32_t i = 0; i < count; ++i) EXPECT_EQ(order[i], i);
        } else {
            EXPECT_LE(nodes[64].height, 6u);
            EXPECT_EQ(nodes[64].first, 32u);
            EXPECT_EQ(nodes[64].last, count - 1);
            // Median partitioning is spatial rather than retaining this reversed
            // canonical order; coverage still consists of exactly the same refs.
            auto sorted = order;
            std::sort(sorted.begin(), sorted.end());
            for (uint32_t i = 0; i < count; ++i) EXPECT_EQ(sorted[i], i);
        }
    }
}

}  // namespace

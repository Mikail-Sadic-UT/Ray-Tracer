/// @file test_bvh.cpp
/// @brief Bounds and deterministic median-builder checks.
#include "bvh/bvh.hpp"

#include <cstring>

#include <gtest/gtest.h>

namespace {

rt::Scene scene_with_primitives(rt::Sphere* sphere, rt::Triangle* triangle) {
    return {sphere, 1, triangle, 1, nullptr, 0, nullptr, 0, {}, 0.0f};
}

TEST(Bvh, EmptyScene) {
    rt::Timings timings;
    const rt::Scene scene{nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
    const rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    EXPECT_TRUE(bvh.nodes.empty());
    EXPECT_TRUE(bvh.prim_refs.empty());
    EXPECT_TRUE(rt::validate_bvh(bvh, scene));
    EXPECT_DOUBLE_EQ(bvh.stats.sah_cost, 0.0);
}

TEST(Bvh, OnePrimitiveIsAValidatedLeaf) {
    rt::Sphere sphere{{1.0f, -2.0f, 3.0f}, 2.0f, 0};
    const rt::Scene scene{&sphere, 1, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
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
    EXPECT_GT(bvh.stats.sah_cost, 0.0);
    EXPECT_TRUE(rt::validate_bvh(bvh, scene, nullptr, params.max_leaf_size));
}

TEST(Bvh, MedianBuildCoversCanonicalSphereThenTriangleOrder) {
    rt::Sphere sphere{{0.0f, 0.0f, 0.0f}, 2.0f, 0};
    rt::Triangle triangle{{3.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.0f}, 0};
    const rt::Scene scene = scene_with_primitives(&sphere, &triangle);
    rt::Timings timings;
    rt::BvhParams params;
    params.max_leaf_size = 1;
    const rt::Bvh bvh = rt::build_bvh(scene, params, timings);
    ASSERT_EQ(bvh.nodes.size(), 3u);
    ASSERT_EQ(bvh.prim_refs.size(), 2u);
    EXPECT_EQ(bvh.prim_refs[0], 0u);
    EXPECT_EQ(bvh.prim_refs[1], rt::PRIM_TRIANGLE_BIT);
    EXPECT_TRUE(rt::validate_bvh(bvh, scene, nullptr, params.max_leaf_size));
    EXPECT_FLOAT_EQ(bvh.nodes[0].bounds.lo.x, -2.0f);
    EXPECT_FLOAT_EQ(bvh.nodes[0].bounds.hi.x, 4.0f);
}

TEST(Bvh, EqualCentroidsUsePrimitiveReferenceTieBreak) {
    rt::Sphere spheres[3] = {{{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}};
    const rt::Scene scene{spheres, 3, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
    rt::Timings timings;
    rt::BvhParams params;
    params.max_leaf_size = 1;
    const rt::Bvh first = rt::build_bvh(scene, params, timings);
    const rt::Bvh second = rt::build_bvh(scene, params, timings);
    EXPECT_EQ(first.prim_refs, second.prim_refs);
    EXPECT_EQ(first.nodes.size(), second.nodes.size());
    EXPECT_TRUE(rt::validate_bvh(first, scene, nullptr, params.max_leaf_size));
}

TEST(Bvh, ValidatorRejectsUnreachableNodes) {
    rt::Sphere sphere{{0, 0, 0}, 1, 0};
    const rt::Scene scene{&sphere, 1, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
    rt::Timings timings;
    rt::Bvh bvh = rt::build_bvh(scene, {}, timings);
    bvh.nodes.push_back(bvh.nodes.front());

    std::string error;
    EXPECT_FALSE(rt::validate_bvh(bvh, scene, &error));
    EXPECT_EQ(error, "bvh: unreachable node");
}

TEST(Bvh, ValidatorRejectsInvalidPrimitiveReference) {
    rt::Sphere sphere{{0, 0, 0}, 1, 0};
    const rt::Scene scene{&sphere, 1, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
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
                      0.25f + static_cast<float>(i % 2) * 0.1f, 0};
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

    ASSERT_TRUE(rt::validate_bvh(one, scene, nullptr, one_thread.max_leaf_size));
    ASSERT_TRUE(rt::validate_bvh(many, scene, nullptr, many_threads.max_leaf_size));
    ASSERT_TRUE(rt::validate_bvh(seventy_two, scene, nullptr, seventy_two_threads.max_leaf_size));
    ASSERT_EQ(one.prim_refs, many.prim_refs);
    ASSERT_EQ(one.prim_refs, seventy_two.prim_refs);
    ASSERT_EQ(one.nodes.size(), many.nodes.size());
    ASSERT_EQ(one.nodes.size(), seventy_two.nodes.size());
    EXPECT_EQ(std::memcmp(one.nodes.data(), many.nodes.data(), one.nodes.size() * sizeof(rt::BvhNode)),
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
    rt::Sphere spheres[4] = {{{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0},
                             {{0, 0, 0}, 1, 0}, {{0, 0, 0}, 1, 0}};
    const rt::Scene scene{spheres, 4, nullptr, 0, nullptr, 0, nullptr, 0, {}, 0.0f};
    rt::BvhParams params;
    params.method = rt::BvhMethod::LBVH;
    params.max_leaf_size = 1;
    params.threads = 4;

    rt::Timings timings;
    const rt::Bvh bvh = rt::build_bvh(scene, params, timings);
    ASSERT_TRUE(rt::validate_bvh(bvh, scene, nullptr, params.max_leaf_size));
    ASSERT_EQ(bvh.prim_refs.size(), 4u);
    EXPECT_EQ(bvh.prim_refs[0], 0u);
    EXPECT_EQ(bvh.prim_refs[1], 1u);
    EXPECT_EQ(bvh.prim_refs[2], 2u);
    EXPECT_EQ(bvh.prim_refs[3], 3u);
}

}  // namespace

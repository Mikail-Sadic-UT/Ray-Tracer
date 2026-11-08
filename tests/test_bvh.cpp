/// @file test_bvh.cpp
/// @brief Bounds and deterministic median-builder checks.
#include "bvh/bvh.hpp"

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

}  // namespace

/// @file test_oracle_self.cpp
/// @brief Checks the double-precision oracle against hand-computed cases before it is used to
///        judge `intersect_sphere` and `intersect_triangle`.
#include <gtest/gtest.h>

#include "oracle.hpp"

namespace {

constexpr float kInf = __builtin_huge_valf();

rt::Ray ray(rt::vec3 origin, rt::vec3 dir) {
    return {origin, 0.0f, dir, kInf};
}

const rt::Sphere kUnit{{0.0f, 0.0f, 0.0f}, 1.0f, 0};
const rt::Triangle kRight{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0};

}  // namespace

/// A ray along the axis of the unit sphere crosses it at z = -1 and z = 1, so from z = -3 the
/// roots are 2 and 4; doubling the direction halves them.
TEST(OracleSelfSphere, AxisRay) {
    const auto a = rt::oracle::sphere(ray({0, 0, -3}, {0, 0, 1}), kUnit);
    ASSERT_TRUE(a.real);
    EXPECT_DOUBLE_EQ(a.t0, 2.0);
    EXPECT_DOUBLE_EQ(a.t1, 4.0);
    EXPECT_DOUBLE_EQ(a.t_closest, 3.0);
    EXPECT_DOUBLE_EQ(a.dist, 0.0);
    EXPECT_DOUBLE_EQ(a.half_chord, 1.0);
    const auto b = rt::oracle::sphere(ray({0, 0, -3}, {0, 0, 2}), kUnit);
    EXPECT_DOUBLE_EQ(b.t0, 1.0);
    EXPECT_DOUBLE_EQ(b.t1, 2.0);
}

/// Offset 0.6 from the axis, the half chord is 0.8 (a 3-4-5 triangle), so the roots are
/// 3 - 0.8 and 3 + 0.8 up to the rounding of 0.6f.
TEST(OracleSelfSphere, OffAxisRay) {
    const float offset = 0.6f;
    const auto a = rt::oracle::sphere(ray({offset, 0, -3}, {0, 0, 1}), kUnit);
    ASSERT_TRUE(a.real);
    EXPECT_NEAR(a.dist, 0.6, 1e-7);
    EXPECT_NEAR(a.half_chord, 0.8, 1e-7);
    EXPECT_NEAR(a.t0, 2.2, 1e-7);
    EXPECT_NEAR(a.t1, 3.8, 1e-7);
}

/// A tangent ray has one double root, and a ray farther than the radius from the center
/// does not meet the sphere.
TEST(OracleSelfSphere, TangentAndMiss) {
    const auto tangent = rt::oracle::sphere(ray({1, 0, -3}, {0, 0, 1}), kUnit);
    ASSERT_TRUE(tangent.real);
    EXPECT_DOUBLE_EQ(tangent.t0, 3.0);
    EXPECT_DOUBLE_EQ(tangent.t1, 3.0);
    const auto miss = rt::oracle::sphere(ray({1.5f, 0, -3}, {0, 0, 1}), kUnit);
    EXPECT_FALSE(miss.real);
    EXPECT_DOUBLE_EQ(miss.dist, 1.5);
}

/// From the center the roots are -1 and 1: one behind the origin, one ahead.
TEST(OracleSelfSphere, OriginInside) {
    const auto a = rt::oracle::sphere(ray({0, 0, 0}, {0, 0, 1}), kUnit);
    ASSERT_TRUE(a.real);
    EXPECT_DOUBLE_EQ(a.t0, -1.0);
    EXPECT_DOUBLE_EQ(a.t1, 1.0);
}

/// A ray straight down onto the right triangle at (0.25, 0.5) hits the plane at t = 2 with
/// barycentrics u = 0.25 (weight of v1) and v = 0.5 (weight of v2).
TEST(OracleSelfTriangle, InteriorHit) {
    const auto a = rt::oracle::triangle(ray({0.25f, 0.5f, 2}, {0, 0, -1}), kRight);
    ASSERT_FALSE(a.degenerate);
    ASSERT_FALSE(a.parallel);
    EXPECT_DOUBLE_EQ(a.t, 2.0);
    EXPECT_DOUBLE_EQ(a.u, 0.25);
    EXPECT_DOUBLE_EQ(a.v, 0.5);
    EXPECT_DOUBLE_EQ(a.margin, 0.25);
    EXPECT_DOUBLE_EQ(a.area_perp, 0.5);
    EXPECT_DOUBLE_EQ(a.plane_distance, 2.0);
    // The farthest vertex from the origin (0.25, 0.5, 2) is v1 = (1, 0, 0).
    EXPECT_DOUBLE_EQ(a.reach, std::sqrt(0.75 * 0.75 + 0.5 * 0.5 + 4.0));
}

/// The test is two-sided and independent of the direction's length: from below, with a
/// doubled direction, the barycentrics are unchanged and t halves.
TEST(OracleSelfTriangle, TwoSidedAndScaled) {
    const auto below = rt::oracle::triangle(ray({0.25f, 0.5f, -2}, {0, 0, 1}), kRight);
    EXPECT_DOUBLE_EQ(below.t, 2.0);
    EXPECT_DOUBLE_EQ(below.u, 0.25);
    EXPECT_DOUBLE_EQ(below.v, 0.5);
    const auto scaled = rt::oracle::triangle(ray({0.25f, 0.5f, 2}, {0, 0, -2}), kRight);
    EXPECT_DOUBLE_EQ(scaled.t, 1.0);
    EXPECT_DOUBLE_EQ(scaled.u, 0.25);
    EXPECT_DOUBLE_EQ(scaled.v, 0.5);
}

/// A vertex has margin 0 and the matching weight 1; a point outside has a negative margin.
TEST(OracleSelfTriangle, VertexAndOutside) {
    const auto vertex = rt::oracle::triangle(ray({1, 0, 2}, {0, 0, -1}), kRight);
    EXPECT_DOUBLE_EQ(vertex.u, 1.0);
    EXPECT_DOUBLE_EQ(vertex.v, 0.0);
    EXPECT_DOUBLE_EQ(vertex.margin, 0.0);
    const auto outside = rt::oracle::triangle(ray({-0.5f, 0.25f, 2}, {0, 0, -1}), kRight);
    EXPECT_DOUBLE_EQ(outside.u, -0.5);
    EXPECT_DOUBLE_EQ(outside.margin, -0.5);
}

/// A ray in the triangle's plane never meets it, and a collinear triangle has no area.
TEST(OracleSelfTriangle, ParallelAndDegenerate) {
    const auto parallel = rt::oracle::triangle(ray({-1, 0.25f, 0}, {1, 0, 0}), kRight);
    EXPECT_TRUE(parallel.parallel);
    EXPECT_DOUBLE_EQ(parallel.plane_distance, 0.0);  // in the plane: coplanar, not offset
    const auto offset = rt::oracle::triangle(ray({-1, 0.25f, 3}, {1, 0, 0}), kRight);
    EXPECT_TRUE(offset.parallel);
    EXPECT_DOUBLE_EQ(offset.plane_distance, 3.0);
    const rt::Triangle line{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, 0};
    EXPECT_TRUE(rt::oracle::triangle(ray({0.5f, 0, 2}, {0, 0, -1}), line).degenerate);
}

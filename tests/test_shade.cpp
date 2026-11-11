/// @file test_shade.cpp
/// @brief Tests for shared normal shading.
#include <gtest/gtest.h>

#include <cmath>

#include "shade/shading.hpp"

namespace {

/// Provides an empty scene with the requested background color.
rt::Scene empty_scene(rt::color background) {
    return {nullptr, 0u, nullptr, 0u, nullptr, 0u, nullptr, 0u, background, 0.0f};
}

/// Checks that normal shading maps a known geometric normal into RGB.
TEST(NormalShading, MapsHitNormalToRgb) {
    const rt::Sphere sphere{{0.0f, 0.0f, 0.0f}, 1.0f, 0u};
    const rt::Material material{};
    rt::Scene scene{&sphere, 1u, nullptr, 0u, &material, 1u, nullptr, 0u, {}, 0.0f};
    const rt::Ray ray{{0.0f, 0.0f, 2.0f}, 0.0f, {0.0f, 0.0f, -1.0f}, 10.0f};
    const rt::Hit hit{1.0f, 0u, 0.0f, 0.0f};
    const rt::PixelResult result = rt::NormalShading{}(ray, hit, scene, 0);
    EXPECT_FLOAT_EQ(result.rgb.x, 0.5f);
    EXPECT_FLOAT_EQ(result.rgb.y, 0.5f);
    EXPECT_FLOAT_EQ(result.rgb.z, 1.0f);
    EXPECT_EQ(result.prim_id, 0u);
    EXPECT_EQ(result.light_mask, 0u);
}

/// Computes a sphere hit point and face-forwarded unit normal with its material ID.
TEST(SurfaceAt, SphereHitPointNormalAndMaterial) {
    const rt::Sphere sphere{{1.0f, 2.0f, 3.0f}, 2.0f, 7u};
    const rt::Material material{};
    rt::Scene scene{&sphere, 1u, nullptr, 0u, &material, 1u, nullptr, 0u, {}, 0.0f};
    const rt::Ray ray{{1.0f, 2.0f, 6.0f}, 0.0f, {0.0f, 0.0f, -1.0f}, 10.0f};
    const rt::Hit hit{1.0f, 0u, 0.0f, 0.0f};

    const rt::SurfacePoint point = rt::surface_at(ray, hit, scene);
    EXPECT_FLOAT_EQ(point.p.x, 1.0f);
    EXPECT_FLOAT_EQ(point.p.y, 2.0f);
    EXPECT_FLOAT_EQ(point.p.z, 5.0f);
    EXPECT_FLOAT_EQ(point.ng.x, 0.0f);
    EXPECT_FLOAT_EQ(point.ng.y, 0.0f);
    EXPECT_FLOAT_EQ(point.ng.z, 1.0f);
    EXPECT_FLOAT_EQ(rt::length(point.ng), 1.0f);
    EXPECT_EQ(point.material_id, 7u);
}

/// Face-forwards the sphere normal when the ray starts inside the sphere.
TEST(SurfaceAt, FaceForwardsSphereNormalFromInside) {
    const rt::Sphere sphere{{0.0f, 0.0f, 0.0f}, 1.0f, 0u};
    const rt::Material material{};
    rt::Scene scene{&sphere, 1u, nullptr, 0u, &material, 1u, nullptr, 0u, {}, 0.0f};
    const rt::Ray ray{{0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, 10.0f};
    const rt::Hit hit{1.0f, 0u, 0.0f, 0.0f};

    const rt::SurfacePoint point = rt::surface_at(ray, hit, scene);
    EXPECT_FLOAT_EQ(point.ng.x, 0.0f);
    EXPECT_FLOAT_EQ(point.ng.y, 0.0f);
    EXPECT_FLOAT_EQ(point.ng.z, -1.0f);
    EXPECT_LT(rt::dot(point.ng, ray.dir), 0.0f);
}

/// Computes a triangle hit position from barycentrics and face-forwards its unit normal.
TEST(SurfaceAt, TriangleHitPointNormalAndMaterial) {
    const rt::Triangle triangle{{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, 9u};
    const rt::Material material{};
    rt::Scene scene{nullptr, 0u, &triangle, 1u, &material, 1u, nullptr, 0u, {}, 0.0f};
    const rt::Ray ray{{0.5f, 1.0f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, 10.0f};
    const rt::Hit hit{1.0f, rt::PRIM_TRIANGLE_BIT, 0.25f, 0.5f};

    const rt::SurfacePoint point = rt::surface_at(ray, hit, scene);
    EXPECT_FLOAT_EQ(point.p.x, 0.5f);
    EXPECT_FLOAT_EQ(point.p.y, 1.0f);
    EXPECT_FLOAT_EQ(point.p.z, 0.0f);
    EXPECT_FLOAT_EQ(rt::length(point.ng), 1.0f);
    EXPECT_LT(rt::dot(point.ng, ray.dir), 0.0f);
    EXPECT_FLOAT_EQ(point.ng.x, 0.0f);
    EXPECT_FLOAT_EQ(point.ng.y, 0.0f);
    EXPECT_FLOAT_EQ(point.ng.z, -1.0f);
    EXPECT_EQ(point.material_id, 9u);
}

/// Flips a back-facing triangle normal toward the viewer before mapping it to color.
TEST(NormalShading, FaceForwardsGeometricNormal) {
    const rt::Triangle triangle{{-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    const rt::Material material{};
    rt::Scene scene{nullptr, 0u, &triangle, 1u, &material, 1u, nullptr, 0u, {}, 0.0f};
    const rt::Ray ray{{0.0f, 0.0f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, 10.0f};
    const rt::Hit hit{1.0f, rt::PRIM_TRIANGLE_BIT, 0.25f, 0.5f};
    const rt::PixelResult result = rt::NormalShading{}(ray, hit, scene, 0);
    EXPECT_FLOAT_EQ(result.rgb.x, 0.5f);
    EXPECT_FLOAT_EQ(result.rgb.y, 0.5f);
    EXPECT_FLOAT_EQ(result.rgb.z, 0.0f);
    EXPECT_EQ(result.light_mask, 0u);
}

/// Returns background color and miss identifiers without accessing scene geometry.
TEST(NormalShading, MissReturnsBackground) {
    const rt::color background{0.2f, 0.4f, 0.8f};
    const rt::Scene scene = empty_scene(background);
    const rt::Ray ray{{}, 0.0f, {0.0f, 0.0f, 1.0f}, 10.0f};
    const rt::Hit miss{10.0f, rt::RT_MISS_PRIM, 0.0f, 0.0f};
    const rt::PixelResult result = rt::NormalShading{}(ray, miss, scene, 0);
    EXPECT_FLOAT_EQ(result.rgb.x, background.x);
    EXPECT_FLOAT_EQ(result.rgb.y, background.y);
    EXPECT_FLOAT_EQ(result.rgb.z, background.z);
    EXPECT_EQ(result.prim_id, rt::RT_MISS_PRIM);
    EXPECT_EQ(result.light_mask, 0u);
}

/// Offsets move the point toward the normal side, by a small relative amount, at any scale.
TEST(OffsetRayOrigin, MovesTowardNormalAtEveryScale) {
    const rt::vec3 normals[] = {{0.0f, 1.0f, 0.0f},
                                {0.0f, -1.0f, 0.0f},
                                {0.57735027f, 0.57735027f, -0.57735027f},
                                {-1.0f, 0.0f, 0.0f}};
    const float scales[] = {1e-3f, 0.02f, 0.5f, 1.0f, 37.0f, 1e3f, 1e6f, 1e12f};
    for (const rt::vec3& n : normals) {
        for (float s : scales) {
            for (float sign : {1.0f, -1.0f}) {
                const rt::vec3 p{1.1f * s * sign, 0.7f * s, -0.3f * s * sign};
                const rt::vec3 o = rt::offset_ray_origin(p, n);
                const rt::vec3 d = o - p;
                EXPECT_TRUE(std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z));
                EXPECT_GT(rt::dot(d, n), 0.0f) << "scale " << s;
                // Every coordinate of p is within 1/32 of zero for s < 0.1, where the offset is
                // the fixed 2^-16 times n. The float add rounds by under 1e-4 of that.
                // Otherwise each coordinate moves by int(256 n_a) ulps. An ulp is at most
                // 2^-23 of the value, so the relative length is at most 256 * 2^-23 = 3.05e-5;
                // the 3.1e-5 bound leaves room for rounding. The lower bound, about half the
                // smallest measured 9.9e-6, fails if the offset shrinks to a few ulps.
                if (s < 0.1f) {
                    EXPECT_NEAR(rt::length(d), 1.0f / 65536.0f, 1e-4f / 65536.0f) << "scale " << s;
                } else {
                    const float rel = rt::length(d) / rt::length(p);
                    EXPECT_LT(rel, 3.1e-5f) << "scale " << s;
                    EXPECT_GT(rel, 5e-6f) << "scale " << s;
                }
            }
        }
    }
}

/// A point exactly on a plane moves off it, including at the origin.
TEST(OffsetRayOrigin, MovesPointsNearAndAtZero) {
    const rt::vec3 up{0.0f, 1.0f, 0.0f};
    for (float x : {0.0f, -0.0f, 1e-9f, 0.01f}) {
        const rt::vec3 o = rt::offset_ray_origin({x, 0.0f, 0.0f}, up);
        EXPECT_GT(o.y, 0.0f);
        EXPECT_EQ(o.x, x);
    }
}

/// The offset of an exactly-planar point leaves the plane by more than rounding error.
TEST(OffsetRayOrigin, OffsetsPastRoundingOfPlaneEquation) {
    const rt::vec3 up{0.0f, 1.0f, 0.0f};
    const float ys[] = {0.0f, 1.0f, 250.0f, -250.0f, 1e5f};
    for (float y : ys) {
        const rt::vec3 o = rt::offset_ray_origin({123.456f, y, -78.9f}, up);
        EXPECT_NE(o.y, y);
    }
}

}  // namespace

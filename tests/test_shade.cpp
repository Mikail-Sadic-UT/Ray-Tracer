/// @file test_shade.cpp
/// @brief Tests for shared normal shading.
#include <gtest/gtest.h>

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

}  // namespace

/// @file test_shade_independent.cpp
/// @brief Lambert + shadow checks against literals from tests/oracle/shading_expected.py.
///
/// The expected numbers come from a separate double-precision implementation of the frozen rules
/// in INTERFACES section 6, not from src/shade. They cover cases test_shade.cpp doesn't:
/// an off-axis sphere normal, a tilted triangle normal with unequal channels, and the color of
/// a multi-light sum.
#include <gtest/gtest.h>

#include <cmath>

#include "shade/shading.hpp"
#include "trace/brute.hpp"

namespace {

void expect_rel(rt::color got, double x, double y, double z, double rel = 1e-5) {
    EXPECT_NEAR(got.x, x, rel * std::fabs(x));
    EXPECT_NEAR(got.y, y, rel * std::fabs(y));
    EXPECT_NEAR(got.z, z, rel * std::fabs(z));
}

rt::Light point_light(rt::vec3 pos, rt::color intensity) {
    return {rt::LIGHT_POINT, pos, intensity};
}

/// Sphere hit 60 degrees off the pole: normal (sin 60, cos 60, 0), light at (0, 4, 0), I = 20.
TEST(ShadeIndependent, OffAxisSpherePoint) {
    const rt::Sphere sphere{{0.0f, 1.0f, 0.0f}, 1.0f, 0u};
    const rt::Material material{{0.8f, 0.1f, 0.1f}, {}, 1.0f, 0u};
    const rt::Light light = point_light({0.0f, 4.0f, 0.0f}, {20.0f, 20.0f, 20.0f});
    const rt::Scene scene{&sphere, 1u, nullptr, 0u, &material, 1u, &light, 1u, {}, 0.05f};
    const rt::vec3 dir = rt::normalize(rt::vec3{-0.8660254f, -0.5f, 0.0f});
    const rt::Ray ray{{5.1961524f, 4.0f, 0.0f}, 0.0f, dir, INFINITY};
    const rt::BruteQuery q{scene};
    const rt::Hit hit = q.closest(ray);
    ASSERT_EQ(hit.prim, 0u);
    const rt::PixelResult r = rt::shade_hit(ray, hit, scene, q);
    expect_rel(r.rgb, 0.4719593977248314, 0.05899492471560393, 0.05899492471560393);
    EXPECT_EQ(r.light_mask, 1u);
}

/// Triangle in the plane y = z with normal (0, -1, 1)/sqrt(2), seen from the +normal side.
TEST(ShadeIndependent, TiltedTriangleUnequalChannels) {
    const rt::Triangle tri{{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 4.0f}, 0u};
    const rt::Material material{{0.6f, 0.3f, 0.9f}, {}, 1.0f, 0u};
    const rt::Light light = point_light({2.0f, 0.0f, 3.0f}, {10.0f, 20.0f, 30.0f});
    const rt::Scene scene{nullptr, 0u, &tri, 1u, &material, 1u, &light, 1u, {}, 0.05f};
    const rt::vec3 from{1.0f, 3.0f, 8.0f};
    const rt::Ray ray{from, 0.0f, rt::normalize(rt::vec3{1.0f, 1.0f, 1.0f} - from), INFINITY};
    const rt::BruteQuery q{scene};
    const rt::Hit hit = q.closest(ray);
    ASSERT_NE(hit.prim & rt::PRIM_TRIANGLE_BIT, 0u);
    const rt::PixelResult r = rt::shade_hit(ray, hit, scene, q);
    expect_rel(r.rgb, 0.8960254037844387, 0.8810254037844387, 3.9421143170299753);
    EXPECT_EQ(r.light_mask, 1u);
}

/// Floor point (1, 0, 0) with three lights: shadowed by the sphere, lit, and below the floor.
TEST(ShadeIndependent, MultiLightColorSum) {
    const rt::Sphere sphere{{0.0f, 1.0f, 0.0f}, 1.0f, 0u};
    const rt::Triangle floor[2] = {
        {{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 0u},
        {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 0u}};
    const rt::Material material{{0.7f, 0.7f, 0.7f}, {}, 1.0f, 0u};
    const rt::Light lights[3] = {point_light({0.0f, 4.0f, 0.0f}, {20.0f, 20.0f, 20.0f}),
                                 point_light({3.0f, 4.0f, 3.0f}, {10.0f, 5.0f, 2.0f}),
                                 point_light({-3.0f, -4.0f, 0.0f}, {9.0f, 9.0f, 9.0f})};
    const rt::Scene scene{&sphere, 1u, floor, 2u, &material, 1u, lights, 3u, {}, 0.05f};
    const rt::vec3 from{1.0f, 5.0f, 10.0f};
    const rt::Ray ray{from, 0.0f, rt::normalize(rt::vec3{1.0f, 0.0f, 0.0f} - from), INFINITY};
    const rt::BruteQuery q{scene};
    const rt::PixelResult r = rt::shade_hit(ray, q.closest(ray), scene, q);
    expect_rel(r.rgb, 0.2142920506537052, 0.1246460253268526, 0.07085841013074104);
    EXPECT_EQ(r.light_mask, 2u);
}

}  // namespace

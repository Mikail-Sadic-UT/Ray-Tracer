/// @file test_shade_independent.cpp
/// @brief Lambert, Blinn-Phong and shadow checks against literals from
/// tests/oracle/shading_expected.py.
///
/// The expected numbers come from a separate double-precision implementation of the frozen rules
/// in INTERFACES section 6, not from src/shade. They cover cases test_shade.cpp doesn't:
/// an off-axis sphere normal, a tilted triangle normal with unequal channels, the color of
/// a multi-light sum, an E2 specular point under point and directional lights, a mirror
/// floor reflecting a lit sphere, an off-center ray through a colored glass sphere, and a tinted
/// shadow under a glass sphere.
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

/// E2: specular floor point (1, 0, 0) seen from (1, 5, 10), shininess 20.5 (fractional, so
/// spec_pow's square-root path runs). Light 0 is a point light, light 1 a directional light given
/// with an unnormalized direction, and light 2 a directional light the sphere blocks.
TEST(ShadeIndependent, SpecularWithPointAndDirectionalLights) {
    const rt::Sphere sphere{{0.0f, 1.0f, 0.0f}, 1.0f, 0u};
    const rt::Triangle floor[2] = {
        {{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 0u},
        {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 0u}};
    const rt::Material material{{0.7f, 0.7f, 0.7f}, {0.5f, 0.4f, 0.3f}, 20.5f, rt::MAT_SPECULAR};
    const rt::Light lights[3] = {point_light({3.0f, 4.0f, 3.0f}, {10.0f, 5.0f, 2.0f}),
                                 {rt::LIGHT_DIRECTIONAL, {-1.0f, -2.0f, -0.5f}, {0.3f, 0.6f, 0.9f}},
                                 {rt::LIGHT_DIRECTIONAL, {1.0f, -1.0f, 0.0f}, {5.0f, 5.0f, 5.0f}}};
    const rt::Scene scene{&sphere, 1u, floor, 2u, &material, 1u, lights, 3u, {}, 0.05f};
    const rt::vec3 from{1.0f, 5.0f, 10.0f};
    const rt::Ray ray{from, 0.0f, rt::normalize(rt::vec3{1.0f, 0.0f, 0.0f} - from), INFINITY};
    const rt::BruteQuery q{scene};
    const rt::PixelResult r = rt::shade_hit(ray, q.closest(ray), scene, q);
    expect_rel(r.rgb, 0.3979296575488013, 0.4917751714388172, 0.6213526054445188);
    EXPECT_EQ(r.light_mask, 3u);
}

/// Mirror floor point (2, 0, 0) seen from (4, 1.5, 0): its own Lambert light plus ks times the
/// lit sphere point the reflected ray reaches. The mask is the floor's own (light 0 reaches it).
TEST(ShadeIndependent, MirrorFloorReflectsLitSphere) {
    const rt::Sphere sphere{{0.0f, 1.0f, 0.0f}, 1.0f, 1u};
    const rt::Triangle floor[2] = {
        {{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 0u},
        {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 0u}};
    const rt::Material materials[2] = {
        {{0.1f, 0.1f, 0.1f}, {0.8f, 0.7f, 0.6f}, 1.0f, rt::MAT_MIRROR},
        {{0.8f, 0.1f, 0.1f}, {}, 1.0f, 0u}};
    const rt::Light light = point_light({3.0f, 4.0f, 2.0f}, {20.0f, 20.0f, 20.0f});
    const rt::Scene scene{&sphere, 1u, floor, 2u, materials, 2u, &light, 1u, {}, 0.05f};
    const rt::vec3 from{4.0f, 1.5f, 0.0f};
    const rt::Ray ray{from, 0.0f, rt::normalize(rt::vec3{2.0f, 0.0f, 0.0f} - from), INFINITY};
    const rt::BruteQuery q{scene};
    const rt::Hit hit = q.closest(ray);
    ASSERT_NE(hit.prim & rt::PRIM_TRIANGLE_BIT, 0u);
    const rt::PixelResult r = rt::shade_hit(ray, hit, scene, q);
    expect_rel(r.rgb, 0.3173421754758055, 0.11320063820130652, 0.10961920772280653);
    EXPECT_EQ(r.light_mask, 1u);
}

/// Colored glass sphere (index 1.5, absorbing to (0.5, 0.8, 0.9) per unit) hit off center from
/// (0.3, 0.2, 5) along -z, in front of the background only. The oracle traces the same
/// reflection and refraction tree recursively in double precision.
TEST(ShadeIndependent, OffCenterRayThroughColoredGlass) {
    const rt::Sphere sphere{{0.0f, 0.0f, 0.0f}, 1.0f, 0u};
    const rt::Material glass{{0.0f, 0.0f, 0.0f}, {0.5f, 0.8f, 0.9f}, 1.5f, rt::MAT_GLASS};
    const rt::Scene scene{&sphere, 1u, nullptr, 0u, &glass, 1u, nullptr, 0u, {0.2f, 0.4f, 0.6f},
                          0.05f};
    const rt::Ray ray{{0.3f, 0.2f, 5.0f}, 0.0f, {0.0f, 0.0f, -1.0f}, INFINITY};
    const rt::BruteQuery q{scene};
    const rt::PixelResult r = rt::shade_hit(ray, q.closest(ray), scene, q);
    // Absorption is measured from surface point to surface point; measured from the offset
    // origins it came out 1.03e-5 off on red, which this tolerance catches.
    expect_rel(r.rgb, 0.056496648088896595, 0.2613977344942609, 0.4898446183163686, 1e-5);
    EXPECT_EQ(r.light_mask, 0u);
}

/// Floor point (0.3, 0, 0.2) under a glass sphere (center (0, 1, 0), radius 0.8, absorbing to
/// (0.4, 0.9, 0.6) per unit), lit by a point light at (1.5, 5, 0.5) whose shadow ray crosses the
/// sphere off center (chord 1.0967): the light arrives tinted by absorb^chord, and its bit is set.
TEST(ShadeIndependent, TintedShadowThroughGlass) {
    const rt::Sphere sphere{{0.0f, 1.0f, 0.0f}, 0.8f, 1u};
    const rt::Triangle floor[2] = {
        {{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 0u},
        {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 0u}};
    const rt::Material materials[2] = {
        {{0.7f, 0.7f, 0.7f}, {}, 1.0f, 0u},
        {{0.0f, 0.0f, 0.0f}, {0.4f, 0.9f, 0.6f}, 1.5f, rt::MAT_GLASS}};
    const rt::Light light = point_light({1.5f, 5.0f, 0.5f}, {30.0f, 30.0f, 30.0f});
    const rt::Scene scene{&sphere, 1u, floor, 2u, materials, 2u, &light, 1u, {}, 0.05f};
    // Looks at the floor point from the side, past the sphere.
    const rt::vec3 from{4.0f, 1.0f, 3.0f};
    const rt::Ray ray{from, 0.0f, rt::normalize(rt::vec3{0.3f, 0.0f, 0.2f} - from), INFINITY};
    const rt::BruteQuery q{scene};
    const rt::Hit hit = q.closest(ray);
    ASSERT_NE(hit.prim & rt::PRIM_TRIANGLE_BIT, 0u);
    const rt::PixelResult r = rt::shade_hit(ray, hit, scene, q);
    expect_rel(r.rgb, 0.3163050524111042, 0.7195464133126718, 0.4738238424182926);
    EXPECT_EQ(r.light_mask, 1u);
}

}  // namespace

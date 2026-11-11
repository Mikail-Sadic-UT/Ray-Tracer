/// @file test_shade.cpp
/// @brief Tests for shared normal shading.
#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

#include "camera/camera.hpp"
#include "shade/shading.hpp"
#include "trace/brute.hpp"

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

/// A scene with one white-ish material and the given point lights, for light_sample tests.
struct LightRig {
    rt::Material material{{0.5f, 0.25f, 1.0f}, {}, 1.0f, 0u};
    std::vector<rt::Light> lights;

    explicit LightRig(std::vector<rt::Light> l) : lights(std::move(l)) {}

    rt::Scene scene() const {
        return {nullptr,   0u,  nullptr,       0u,
                &material, 1u,  lights.data(), static_cast<uint32_t>(lights.size()),
                {},        0.0f};
    }
};

rt::Light point_light(rt::vec3 pos, rt::color intensity) {
    return {rt::LIGHT_POINT, pos, intensity};
}

/// A light straight above the surface gives albedo * I / d2 and a shadow ray to the light.
TEST(LightSample, OverheadLightContributionAndShadowRay) {
    const LightRig rig({point_light({0.0f, 2.0f, 0.0f}, {8.0f, 8.0f, 8.0f})});
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    rt::color c{};
    rt::Ray ray{};
    ASSERT_TRUE(rt::light_sample(sp, {0.0f, 1.0f, 0.0f}, rig.scene(), 0u, c, ray));
    EXPECT_FLOAT_EQ(c.x, 0.5f * 8.0f / 4.0f);
    EXPECT_FLOAT_EQ(c.y, 0.25f * 8.0f / 4.0f);
    EXPECT_FLOAT_EQ(c.z, 1.0f * 8.0f / 4.0f);
    EXPECT_EQ(ray.tmin, 0.0f);
    // Near zero the offset is exactly 2^-16 along the normal, so the shadow ray starts there and
    // runs 2 - 2^-16 to the light.
    EXPECT_FLOAT_EQ(ray.origin.y, 1.0f / 65536.0f);
    EXPECT_FLOAT_EQ(rt::length(ray.origin - sp.p), 1.0f / 65536.0f);
    EXPECT_FLOAT_EQ(ray.tmax, 2.0f - 1.0f / 65536.0f);
    EXPECT_FLOAT_EQ(rt::length(ray.dir), 1.0f);
    EXPECT_FLOAT_EQ(ray.dir.y, 1.0f);
}

/// Contribution follows the cosine and 1/d2: a light at 45 degrees and distance sqrt(2).
TEST(LightSample, CosineAndInverseSquareFalloff) {
    const LightRig rig({point_light({1.0f, 1.0f, 0.0f}, {4.0f, 4.0f, 4.0f})});
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    rt::color c{};
    rt::Ray ray{};
    ASSERT_TRUE(rt::light_sample(sp, {0.0f, 1.0f, 0.0f}, rig.scene(), 0u, c, ray));
    const float cosine = 1.0f / std::sqrt(2.0f);
    EXPECT_FLOAT_EQ(c.x, 0.5f * cosine * 4.0f / 2.0f);
}

/// Doubling the distance quarters the contribution.
TEST(LightSample, InverseSquareLaw) {
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    const LightRig near_rig({point_light({0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f})});
    const LightRig far_rig({point_light({0.0f, 2.0f, 0.0f}, {1.0f, 1.0f, 1.0f})});
    rt::color a{}, b{};
    rt::Ray ray{};
    ASSERT_TRUE(rt::light_sample(sp, {}, near_rig.scene(), 0u, a, ray));
    ASSERT_TRUE(rt::light_sample(sp, {}, far_rig.scene(), 0u, b, ray));
    EXPECT_FLOAT_EQ(b.x / a.x, 0.25f);
}

/// A light behind the surface, or exactly edge-on, adds nothing and leaves outputs untouched.
TEST(LightSample, BackFacingAndEdgeOnLightsAreRejected) {
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    for (rt::vec3 pos : {rt::vec3{0.0f, -2.0f, 0.0f}, rt::vec3{3.0f, 0.0f, 0.0f}}) {
        const LightRig rig({point_light(pos, {1.0f, 1.0f, 1.0f})});
        rt::color c{7.0f, 7.0f, 7.0f};
        rt::Ray ray{{9.0f, 9.0f, 9.0f}, 9.0f, {9.0f, 9.0f, 9.0f}, 9.0f};
        EXPECT_FALSE(rt::light_sample(sp, {}, rig.scene(), 0u, c, ray));
        EXPECT_EQ(c.x, 7.0f);
        EXPECT_EQ(ray.tmax, 9.0f);
    }
}

/// A light at the surface point, or a squared distance that is not a normal float, is rejected.
TEST(LightSample, CoincidentOrDegenerateDistanceIsRejected) {
    const rt::SurfacePoint sp{{1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    for (rt::vec3 pos : {rt::vec3{1.0f, 2.0f, 3.0f}, rt::vec3{1.0f, 2.0f + 1e-21f, 3.0f}}) {
        const LightRig rig({point_light(pos, {1.0f, 1.0f, 1.0f})});
        rt::color c{};
        rt::Ray ray{};
        EXPECT_FALSE(rt::light_sample(sp, {}, rig.scene(), 0u, c, ray));
    }
}

/// Core shading ignores directional lights.
TEST(LightSample, DirectionalLightIsNotCoreShaded) {
    const LightRig rig({{rt::LIGHT_DIRECTIONAL, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}});
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    rt::color c{};
    rt::Ray ray{};
    EXPECT_FALSE(rt::light_sample(sp, {}, rig.scene(), 0u, c, ray));
}

/// Sphere above a floor with one point light, for the analytic shade_hit cases.
///
/// Light at (0, 4, 0) with intensity 20, sphere of radius 1 at (0, 1, 0), floor y = 0.
struct LitRig {
    rt::Sphere sphere{{0.0f, 1.0f, 0.0f}, 1.0f, 0u};
    rt::Triangle floor[2] = {{{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 1u},
                             {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 1u}};
    rt::Material materials[2] = {{{0.8f, 0.1f, 0.1f}, {}, 1.0f, 0u},
                                 {{0.7f, 0.7f, 0.7f}, {}, 1.0f, 0u}};
    rt::Light light = point_light({0.0f, 4.0f, 0.0f}, {20.0f, 20.0f, 20.0f});
    rt::Scene scene{&sphere, 1u, floor, 2u, materials, 2u, &light, 1u, {0.1f, 0.1f, 0.15f}, 0.05f};

    /// Shades the closest hit of the ray from @p from toward @p to.
    rt::PixelResult shade(rt::vec3 from, rt::vec3 to, rt::Hit* hit_out = nullptr) const {
        const rt::vec3 d = rt::normalize(to - from);
        const rt::Ray ray{from, 0.0f, d, INFINITY};
        const rt::BruteQuery q{scene};
        const rt::Hit hit = q.closest(ray);
        if (hit_out) *hit_out = hit;
        return rt::shade_hit(ray, hit, scene, q);
    }
};

/// Relative closeness at the quality protocol's 1e-5 tolerance.
void expect_rel(rt::color got, rt::color want, float rel = 1e-5f) {
    EXPECT_NEAR(got.x, want.x, rel * std::fabs(want.x));
    EXPECT_NEAR(got.y, want.y, rel * std::fabs(want.y));
    EXPECT_NEAR(got.z, want.z, rel * std::fabs(want.z));
}

/// A miss returns the background, the miss id and an empty mask.
TEST(ShadeHit, MissReturnsBackground) {
    const LitRig rig;
    const rt::PixelResult r = rig.shade({0.0f, 10.0f, 9.0f}, {0.0f, 10.0f, 0.0f});
    EXPECT_EQ(r.prim_id, rt::RT_MISS_PRIM);
    EXPECT_EQ(r.light_mask, 0u);
    EXPECT_EQ(r.rgb.x, 0.1f);
    EXPECT_EQ(r.rgb.z, 0.15f);
}

/// Lambert value at the sphere's pole: normal and light direction coincide, d = 2.
TEST(ShadeHit, SpherePoleLambert) {
    const LitRig rig;
    rt::Hit hit{};
    const rt::PixelResult r = rig.shade({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, &hit);
    ASSERT_EQ(hit.prim, 0u);
    // albedo * ambient + albedo * I / d2, with d2 = 4.
    expect_rel(r.rgb, {0.8f * 0.05f + 0.8f * 20.0f / 4.0f, 0.1f * 0.05f + 0.1f * 20.0f / 4.0f,
                       0.1f * 0.05f + 0.1f * 20.0f / 4.0f});
    EXPECT_EQ(r.prim_id, 0u);
    EXPECT_EQ(r.light_mask, 1u);
}

/// A lit floor point at (3, 0, 0): cos = 4/5, d2 = 25, and the sphere does not block.
TEST(ShadeHit, LitFloorPointCosineAndFalloff) {
    const LitRig rig;
    rt::Hit hit{};
    const rt::PixelResult r = rig.shade({3.0f, 5.0f, 10.0f}, {3.0f, 0.0f, 0.0f}, &hit);
    ASSERT_NE(hit.prim & rt::PRIM_TRIANGLE_BIT, 0u);
    const float want = 0.7f * 0.05f + 0.7f * 0.8f * 20.0f / 25.0f;
    expect_rel(r.rgb, {want, want, want});
    EXPECT_EQ(r.light_mask, 1u);
}

/// Contribution falls as 1/d2: the same floor normal at twice the light height gives a quarter.
TEST(ShadeHit, InverseSquareOnFloor) {
    LitRig rig;
    const rt::vec3 from{4.0f, 5.0f, 10.0f};
    const rt::vec3 to{4.0f, 0.0f, 0.0f};
    rig.light.pos_or_dir = {4.0f, 1.0f, 0.0f};
    const float near_c = rig.shade(from, to).rgb.x - 0.7f * 0.05f;
    rig.light.pos_or_dir = {4.0f, 2.0f, 0.0f};
    const float far_c = rig.shade(from, to).rgb.x - 0.7f * 0.05f;
    // Both contributions are exact to a few ulps; the subtraction of the ambient term adds little.
    EXPECT_NEAR(far_c / near_c, 0.25f, 1e-6f);
}

/// The floor point under the sphere is shadowed: ambient only, with the mask bit clear.
TEST(ShadeHit, ShadowedFloorPointIsAmbientOnly) {
    const LitRig rig;
    rt::Hit hit{};
    const rt::PixelResult r = rig.shade({1.0f, 5.0f, 10.0f}, {1.0f, 0.0f, 0.0f}, &hit);
    ASSERT_NE(hit.prim & rt::PRIM_TRIANGLE_BIT, 0u);
    expect_rel(r.rgb, {0.7f * 0.05f, 0.7f * 0.05f, 0.7f * 0.05f});
    EXPECT_EQ(r.light_mask, 0u);
}

/// The sphere's shadow edge on the floor lies at x = sqrt(2): the tangent cone from a light at
/// height 4 around a unit sphere centered at height 1 has radius 4 * tan(asin(1/3)) there.
TEST(ShadeHit, ShadowBoundaryLocation) {
    const LitRig rig;
    const auto lit = [&](float px) {
        return rig.shade({px, 10.0f, 0.0f}, {px, 0.0f, 0.0f}).light_mask == 1u;
    };
    float shadowed = 1.2f;
    float lit_x = 1.7f;
    ASSERT_FALSE(lit(shadowed));
    ASSERT_TRUE(lit(lit_x));
    for (int i = 0; i < 40; ++i) {
        const float mid = 0.5f * (shadowed + lit_x);
        (lit(mid) ? lit_x : shadowed) = mid;
    }
    // The shadow ray starts 2^-16 above the floor, which moves the edge by less than that; the
    // bound is 2 * 2^-16. The measured difference is 5e-6.
    EXPECT_NEAR(lit_x, std::sqrt(2.0f), 2.0f / 65536.0f);
}

/// A large unoccluded floor shows no acne: every point is lit with the analytic value.
TEST(ShadeHit, NoAcneOnLargeUnoccludedPlane) {
    LitRig rig;
    rig.sphere.center = {0.0f, 1000.0f, 0.0f};  // out of the way
    rig.sphere.radius = 0.5f;
    const float big = 1000.0f;
    rig.floor[0] = {{-big, 0.0f, -big}, {-big, 0.0f, big}, {big, 0.0f, big}, 1u};
    rig.floor[1] = {{-big, 0.0f, -big}, {big, 0.0f, big}, {big, 0.0f, -big}, 1u};
    rig.light.pos_or_dir = {0.7f, 3.0f, 0.4f};
    for (int i = -9; i <= 9; ++i) {
        for (int j = -9; j <= 9; ++j) {
            const float x = 97.3f * static_cast<float>(i) + 0.123f;
            const float z = 91.1f * static_cast<float>(j) - 0.321f;
            const rt::PixelResult r = rig.shade({x, 50.0f, z + 5.0f}, {x, 0.0f, z});
            const float dx = 0.7f - x;
            const float dz = 0.4f - z;
            const float d2 = dx * dx + 9.0f + dz * dz;
            const float cosine = 3.0f / std::sqrt(d2);
            const float want = 0.7f * 0.05f + 0.7f * cosine * 20.0f / d2;
            EXPECT_EQ(r.light_mask, 1u) << x << ", " << z;
            // The hit point comes from barycentrics over a 2000-wide triangle, so each coordinate
            // carries up to about 6e-5 (one ulp at 1000). That matters only close to the light:
            // the one grid point at d = 3.1 is off by 1.1e-5. Everywhere with d >= 30 the
            // measured worst is 6e-8, so those are checked to 1e-6. The near point is checked by
            // its light_mask alone.
            if (d2 >= 900.0f) expect_rel(r.rgb, {want, want, want}, 1e-6f);
        }
    }
}

/// Query that counts shadow rays and reports every one as unoccluded.
struct CountingQuery {
    mutable int shadow_rays = 0;
    rt::Hit closest(const rt::Ray&) const { return {}; }
    bool any(const rt::Ray&) const {
        ++shadow_rays;
        return false;
    }
};

/// A light behind the surface casts no shadow ray; a light in front casts exactly one.
TEST(ShadeHit, BackFacingLightCastsNoShadowRay) {
    LitRig rig;
    const rt::Ray ray{
        {3.0f, 5.0f, 10.0f}, 0.0f, rt::normalize(rt::vec3{0.0f, -5.0f, -10.0f}), INFINITY};
    const rt::Hit hit = rt::BruteQuery{rig.scene}.closest(ray);
    ASSERT_NE(hit.prim & rt::PRIM_TRIANGLE_BIT, 0u);
    const CountingQuery q;
    rig.light.pos_or_dir = {3.0f, -4.0f, 0.0f};  // below the floor, ray comes from above
    const rt::PixelResult behind = rt::shade_hit(ray, hit, rig.scene, q);
    EXPECT_EQ(q.shadow_rays, 0);
    EXPECT_EQ(behind.light_mask, 0u);
    rig.light.pos_or_dir = {3.0f, 4.0f, 0.0f};
    const rt::PixelResult front = rt::shade_hit(ray, hit, rig.scene, q);
    EXPECT_EQ(q.shadow_rays, 1);
    EXPECT_EQ(front.light_mask, 1u);
}

/// Several lights accumulate in index order, and only unoccluded ones set their bit.
TEST(ShadeHit, MultipleLightsAccumulateInOrderWithMask) {
    LitRig rig;
    const rt::Light lights[3] = {point_light({0.0f, 4.0f, 0.0f}, {20.0f, 20.0f, 20.0f}),
                                 point_light({3.0f, 4.0f, 3.0f}, {10.0f, 5.0f, 2.0f}),
                                 point_light({-3.0f, -4.0f, 0.0f}, {9.0f, 9.0f, 9.0f})};
    rig.scene.lights = lights;
    rig.scene.num_lights = 3u;
    // The point (1, 0, 0) is shadowed from light 0 by the sphere, lit by light 1, and light 2
    // is below the floor.
    const rt::Ray ray{
        {1.0f, 5.0f, 10.0f}, 0.0f, rt::normalize(rt::vec3{0.0f, -5.0f, -10.0f}), INFINITY};
    const rt::BruteQuery q{rig.scene};
    const rt::PixelResult r = rt::shade_hit(ray, q.closest(ray), rig.scene, q);
    EXPECT_EQ(r.light_mask, 2u);
    rt::SurfacePoint sp = rt::surface_at(ray, q.closest(ray), rig.scene);
    rt::color c1{};
    rt::Ray shadow{};
    ASSERT_TRUE(rt::light_sample(sp, {}, rig.scene, 1u, c1, shadow));
    const rt::color want = rig.materials[1].albedo * rig.scene.ambient + c1;
    EXPECT_EQ(r.rgb.x, want.x);
    EXPECT_EQ(r.rgb.y, want.y);
    EXPECT_EQ(r.rgb.z, want.z);
}

}  // namespace

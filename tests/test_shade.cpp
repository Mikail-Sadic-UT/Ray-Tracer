/// @file test_shade.cpp
/// @brief Tests for shared normal shading.
#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

#include "camera/camera.hpp"
#include "shade/render_pixel.hpp"
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

/// A directional light gives albedo * cos * I with no distance falloff, and an unbounded shadow
/// ray along -dir from the offset origin.
TEST(LightSample, DirectionalLightHasNoFalloffAndUnboundedShadowRay) {
    const float c45 = 1.0f / std::sqrt(2.0f);
    // Travels down and toward -x, so the surface sees it from +x at 45 degrees.
    const LightRig rig({{rt::LIGHT_DIRECTIONAL, {-c45, -c45, 0.0f}, {2.0f, 2.0f, 2.0f}}});
    for (rt::vec3 p : {rt::vec3{0.0f, 0.0f, 0.0f}, rt::vec3{100.0f, 0.0f, -50.0f}}) {
        const rt::SurfacePoint sp{p, {0.0f, 1.0f, 0.0f}, 0u};
        rt::color c{};
        rt::Ray ray{};
        ASSERT_TRUE(rt::light_sample(sp, {0.0f, 1.0f, 0.0f}, rig.scene(), 0u, c, ray));
        EXPECT_FLOAT_EQ(c.x, 0.5f * c45 * 2.0f);
        EXPECT_FLOAT_EQ(c.z, 1.0f * c45 * 2.0f);
        EXPECT_EQ(ray.tmin, 0.0f);
        EXPECT_EQ(ray.tmax, INFINITY);
        EXPECT_FLOAT_EQ(ray.dir.x, c45);
        EXPECT_FLOAT_EQ(ray.dir.y, c45);
        EXPECT_GT(ray.origin.y, p.y);
    }
    // From below the surface it adds nothing.
    const LightRig up({{rt::LIGHT_DIRECTIONAL, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}});
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    rt::color c{};
    rt::Ray ray{};
    EXPECT_FALSE(rt::light_sample(sp, {0.0f, 1.0f, 0.0f}, up.scene(), 0u, c, ray));
}

/// An unknown light type contributes nothing.
TEST(LightSample, UnknownLightTypeIsIgnored) {
    const LightRig rig({{7u, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}});
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    rt::color c{};
    rt::Ray ray{};
    EXPECT_FALSE(rt::light_sample(sp, {0.0f, 1.0f, 0.0f}, rig.scene(), 0u, c, ray));
}

/// spec_pow agrees with pow in double for integer, fractional and huge exponents, is exact for
/// small integer powers of exactly representable values, and clamps its base.
TEST(SpecPow, MatchesPowAndClampsBase) {
    EXPECT_EQ(rt::spec_pow(0.5f, 1.0f), 0.5f);
    EXPECT_EQ(rt::spec_pow(0.5f, 8.0f), 1.0f / 256.0f);
    EXPECT_EQ(rt::spec_pow(0.0f, 4.0f), 0.0f);
    EXPECT_EQ(rt::spec_pow(-0.3f, 4.0f), 0.0f);
    EXPECT_EQ(rt::spec_pow(1.0f, 1000.0f), 1.0f);
    EXPECT_EQ(rt::spec_pow(NAN, 2.0f), 0.0f);
    const float xs[] = {0.01f, 0.3f, 0.7f, 0.9f, 0.99f, 0.999f, 0.99999f};
    const float ns[] = {1.0f, 1.5f, 2.25f, 7.0f, 10.3f, 32.0f, 64.0f, 128.7f, 1000.0f, 3e7f};
    for (float x : xs) {
        for (float n : ns) {
            const double want = std::pow(static_cast<double>(x), static_cast<double>(n));
            const float got = rt::spec_pow(x, n);
            if (want < 1e-30) {
                EXPECT_LT(got, 1e-25f) << x << "^" << n;
                continue;
            }
            // Each multiply rounds once; 1e-4 relative covers ~30 roundings plus the 16-bit
            // fraction (n * |log x| * 2^-16 at most).
            const double tol = 1e-4 + static_cast<double>(n) * std::fabs(std::log(x)) / 65536.0;
            EXPECT_NEAR(got / want, 1.0, tol) << x << "^" << n;
        }
    }
}

/// MAT_SPECULAR adds ks * (n.h)^shininess * I / d2; with the viewer on the mirror direction the
/// half vector is the normal, so the highlight is exactly ks * I / d2.
TEST(LightSample, BlinnPhongHighlight) {
    rt::Material spec{{0.5f, 0.25f, 1.0f}, {0.4f, 0.4f, 0.4f}, 16.0f, rt::MAT_SPECULAR};
    rt::Light light{rt::LIGHT_POINT, {1.0f, 1.0f, 0.0f}, {4.0f, 4.0f, 4.0f}};
    const rt::Scene scene{nullptr, 0u, nullptr, 0u, &spec, 1u, &light, 1u, {}, 0.0f};
    const rt::SurfacePoint sp{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0u};
    const float c45 = 1.0f / std::sqrt(2.0f);
    rt::color c{};
    rt::Ray ray{};
    // Mirror direction of l = (c45, c45, 0) about +y.
    ASSERT_TRUE(rt::light_sample(sp, {-c45, c45, 0.0f}, scene, 0u, c, ray));
    const float diffuse = 0.5f * c45 * 4.0f / 2.0f;
    EXPECT_NEAR(c.x, diffuse + 0.4f * 4.0f / 2.0f, 1e-5f);

    // Viewer straight above: h is 22.5 degrees off the normal.
    ASSERT_TRUE(rt::light_sample(sp, {0.0f, 1.0f, 0.0f}, scene, 0u, c, ray));
    const double nh = std::cos(3.14159265358979323846 / 8.0);
    EXPECT_NEAR(c.x, diffuse + 0.4 * std::pow(nh, 16.0) * 2.0, 1e-5);

    // Without the flag, ks is ignored (Core behavior, unchanged).
    spec.flags = 0u;
    ASSERT_TRUE(rt::light_sample(sp, {-c45, c45, 0.0f}, scene, 0u, c, ray));
    EXPECT_FLOAT_EQ(c.x, diffuse);
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

/// LitShading is the default policy of render_pixel and equals shade_hit on the same hit.
TEST(LitShading, IsRenderPixelDefault) {
    const LitRig rig;
    const rt::Camera cam{
        {3.0f, 5.0f, 10.0f}, {3.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 40.0f, 8u, 8u};
    const rt::BruteQuery q{rig.scene};
    for (uint32_t py = 0; py < 8u; ++py) {
        for (uint32_t px = 0; px < 8u; ++px) {
            const rt::PixelResult a = rt::render_pixel<rt::BruteQuery>(cam, px, py, rig.scene, q);
            const rt::PixelResult b = rt::render_pixel(cam, px, py, rig.scene, q, rt::LitShading{});
            EXPECT_EQ(a.rgb.x, b.rgb.x);
            EXPECT_EQ(a.rgb.y, b.rgb.y);
            EXPECT_EQ(a.rgb.z, b.rgb.z);
            EXPECT_EQ(a.prim_id, b.prim_id);
            EXPECT_EQ(a.light_mask, b.light_mask);
        }
    }
}

/// Two big floor-like triangles at height @p y with material @p m, for the mirror tests.
void plane(rt::Triangle* out, float y, uint32_t m) {
    const float big = 100.0f;
    out[0] = {{-big, y, -big}, {-big, y, big}, {big, y, big}, m};
    out[1] = {{-big, y, -big}, {big, y, big}, {big, y, -big}, m};
}

/// A mirror whose reflection misses everything shows ks * background on top of its own direct
/// light; the AOVs describe the mirror itself.
TEST(Mirror, ReflectionThatMissesShowsTintedBackground) {
    rt::Triangle floor[2];
    plane(floor, 0.0f, 0u);
    const rt::Material mirror{{0.0f, 0.0f, 0.0f}, {0.5f, 0.25f, 1.0f}, 1.0f, rt::MAT_MIRROR};
    const rt::Scene scene{nullptr, 0u, floor, 2u, &mirror, 1u, nullptr, 0u, {0.2f, 0.4f, 0.6f},
                          0.05f};
    const rt::vec3 from{0.0f, 1.0f, 1.0f};
    const rt::Ray ray{from, 0.0f, rt::normalize(rt::vec3{0.0f, 0.0f, 0.0f} - from), INFINITY};
    const rt::BruteQuery q{scene};
    const rt::Hit hit = q.closest(ray);
    const rt::PixelResult r = rt::shade_hit(ray, hit, scene, q);
    EXPECT_EQ(r.prim_id, hit.prim);
    EXPECT_NE(r.prim_id & rt::PRIM_TRIANGLE_BIT, 0u);
    EXPECT_EQ(r.light_mask, 0u);
    EXPECT_FLOAT_EQ(r.rgb.x, 0.5f * 0.2f);
    EXPECT_FLOAT_EQ(r.rgb.y, 0.25f * 0.4f);
    EXPECT_FLOAT_EQ(r.rgb.z, 1.0f * 0.6f);
}

/// Looking straight down at a mirror floor under a sphere shows the sphere's underside, which
/// the light above cannot reach: ks * albedo_sphere * ambient, plus the floor's own ambient.
TEST(Mirror, SeesTheObjectAboveIt) {
    const rt::Sphere sphere{{0.0f, 2.0f, 0.0f}, 0.5f, 1u};
    rt::Triangle floor[2];
    plane(floor, 0.0f, 0u);
    const rt::Material materials[2] = {
        {{0.1f, 0.1f, 0.1f}, {0.8f, 0.8f, 0.8f}, 1.0f, rt::MAT_MIRROR},
        {{0.9f, 0.3f, 0.1f}, {}, 1.0f, 0u}};
    const rt::Light light{rt::LIGHT_POINT, {0.0f, 10.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const rt::Scene scene{&sphere, 1u, floor, 2u, materials, 2u, &light, 1u, {}, 0.05f};
    // Starts between the sphere and the floor so the primary ray hits the floor at the origin.
    const rt::Ray ray{{0.0f, 1.0f, 0.0f}, 0.0f, {0.0f, -1.0f, 0.0f}, INFINITY};
    const rt::BruteQuery q{scene};
    const rt::PixelResult r = rt::shade_hit(ray, q.closest(ray), scene, q);
    // The floor point is shadowed by the sphere, so its own light is ambient only.
    EXPECT_EQ(r.light_mask, 0u);
    const float floor_ambient = 0.1f * 0.05f;
    EXPECT_FLOAT_EQ(r.rgb.x, floor_ambient + 0.8f * (0.9f * 0.05f));
    EXPECT_FLOAT_EQ(r.rgb.y, floor_ambient + 0.8f * (0.3f * 0.05f));
    EXPECT_FLOAT_EQ(r.rgb.z, floor_ambient + 0.8f * (0.1f * 0.05f));
}

/// Two parallel mirrors bounce a vertical ray forever; shading stops after
/// RT_MAX_BOUNCES reflections, so the sum is a * (1 + k + k^2 + k^3 + k^4).
TEST(Mirror, StopsAfterTheBounceLimit) {
    rt::Triangle planes[4];
    plane(planes, 0.0f, 0u);
    plane(planes + 2, 1.0f, 0u);
    const rt::Material mirror{{0.4f, 0.4f, 0.4f}, {0.5f, 0.5f, 0.5f}, 1.0f, rt::MAT_MIRROR};
    const rt::Scene scene{nullptr, 0u, planes, 4u, &mirror, 1u, nullptr, 0u, {9.0f, 9.0f, 9.0f},
                          1.0f};
    const rt::Ray ray{{0.25f, 0.5f, 0.25f}, 0.0f, {0.0f, -1.0f, 0.0f}, INFINITY};
    const rt::BruteQuery q{scene};
    const rt::PixelResult r = rt::shade_hit(ray, q.closest(ray), scene, q);
    static_assert(rt::RT_MAX_BOUNCES == 4u, "the expected sum below assumes 4 bounces");
    const float want = 0.4f * (1.0f + 0.5f + 0.25f + 0.125f + 0.0625f);
    EXPECT_FLOAT_EQ(r.rgb.x, want);
    EXPECT_FLOAT_EQ(r.rgb.z, want);
}

/// A black mirror (ks = 0) traces no reflection ray, and a non-mirror material ignores ks.
TEST(Mirror, ZeroReflectanceAndNonMirrorAddNothing) {
    rt::Triangle floor[2];
    plane(floor, 0.0f, 0u);
    rt::Material material{{0.2f, 0.2f, 0.2f}, {0.0f, 0.0f, 0.0f}, 1.0f, rt::MAT_MIRROR};
    const rt::Scene scene{nullptr, 0u, floor, 2u, &material, 1u, nullptr, 0u, {1.0f, 1.0f, 1.0f},
                          0.5f};
    const rt::Ray ray{
        {0.0f, 1.0f, 1.0f}, 0.0f, rt::normalize(rt::vec3{0.0f, -1.0f, -1.0f}), INFINITY};
    const rt::BruteQuery q{scene};
    EXPECT_FLOAT_EQ(rt::shade_hit(ray, q.closest(ray), scene, q).rgb.x, 0.1f);
    material.ks = {1.0f, 1.0f, 1.0f};
    material.flags = 0u;
    EXPECT_FLOAT_EQ(rt::shade_hit(ray, q.closest(ray), scene, q).rgb.x, 0.1f);
}

/// A sphere of glass with index 1.5, absorbing to @p absorb per unit, alone in front of a
/// background; albedo 0 and no lights, so only reflection, refraction and absorption show.
struct GlassRig {
    rt::Sphere sphere{{0.0f, 0.0f, 0.0f}, 1.0f, 0u};
    rt::Material glass;
    rt::Scene scene;
    explicit GlassRig(rt::color absorb, float ior = 1.5f)
        : glass{{0.0f, 0.0f, 0.0f}, absorb, ior, rt::MAT_GLASS},
          scene{&sphere, 1u, nullptr, 0u, &glass, 1u, nullptr, 0u, {0.2f, 0.4f, 0.6f}, 0.05f} {}
    rt::PixelResult shade(rt::vec3 from, rt::vec3 dir) const {
        const rt::Ray ray{from, 0.0f, rt::normalize(dir), INFINITY};
        const rt::BruteQuery q{scene};
        return rt::shade_hit(ray, q.closest(ray), scene, q);
    }
};

/// Straight through the center: R = ((1 - 1.5) / 2.5)^2 = 0.04 at every surface, and each
/// crossing of the diameter absorbs absorb^2. Within the 4-bounce limit the paths that reach the
/// background are: reflect at the front (0.04); in, out the back (0.96 * a^2 * 0.96); in, reflect
/// inside, out the front (0.96 * a^2 * 0.04 * a^2 * 0.96); and in, reflect inside twice, out the
/// back (0.96 * a^2 * 0.04 * a^2 * 0.04 * a^2 * 0.96). Each sum multiplies the background.
TEST(Glass, NormalIncidenceFresnelAndAbsorption) {
    const GlassRig rig({0.5f, 1.0f, 0.8f});
    const rt::PixelResult r = rig.shade({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f});
    EXPECT_EQ(r.prim_id, 0u);
    EXPECT_EQ(r.light_mask, 0u);
    const double bg[3] = {0.2, 0.4, 0.6};
    const double a[3] = {0.5, 1.0, 0.8};
    const float got[3] = {r.rgb.x, r.rgb.y, r.rgb.z};
    for (int c = 0; c < 3; ++c) {
        const double a2 = a[c] * a[c];
        const double want = (0.04 + 0.96 * a2 * 0.96 + 0.96 * a2 * 0.04 * a2 * 0.96 +
                             0.96 * a2 * 0.04 * a2 * 0.04 * a2 * 0.96) *
                            bg[c];
        EXPECT_NEAR(got[c], want, 1e-5 * want) << "channel " << c;
    }
}

/// A ray inside glass meeting the surface past the critical angle (sin > 1 / 1.5) is reflected
/// completely: its full weight comes back as background, with no refracted part.
TEST(Glass, TotalInternalReflection) {
    rt::Triangle floor[2];
    plane(floor, 0.0f, 0u);  // outward normal +y, so a ray from below leaves the glass
    const rt::Material glass{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 1.5f, rt::MAT_GLASS};
    const rt::Scene scene{nullptr, 0u, floor, 2u, &glass, 1u, nullptr, 0u, {0.2f, 0.4f, 0.6f},
                          0.0f};
    const rt::BruteQuery q{scene};
    // 60 degrees from the normal: sin 60 * 1.5 = 1.3 > 1.
    const rt::Ray steep{
        {0.0f, -1.0f, 0.0f}, 0.0f, rt::normalize(rt::vec3{std::sqrt(3.0f), 1.0f, 0.0f}), INFINITY};
    const rt::PixelResult tir = rt::shade_hit(steep, q.closest(steep), scene, q);
    EXPECT_FLOAT_EQ(tir.rgb.x, 0.2f);
    EXPECT_FLOAT_EQ(tir.rgb.z, 0.6f);
    // 20 degrees: below the critical angle, both rays leave, and with no absorption and only
    // the background to see, their weights still add up to 1.
    const rt::Ray shallow{
        {0.0f, -1.0f, 0.0f}, 0.0f, rt::normalize(rt::vec3{0.36397f, 1.0f, 0.0f}), INFINITY};
    const rt::PixelResult part = rt::shade_hit(shallow, q.closest(shallow), scene, q);
    EXPECT_NEAR(part.rgb.x, 0.2f, 1e-6f);
}

/// Index 1 bends nothing and reflects nothing (Schlick's R0 is 0), so clear glass of index 1
/// shows the background unchanged at normal incidence.
TEST(Glass, IndexOneIsInvisible) {
    const GlassRig rig({1.0f, 1.0f, 1.0f}, 1.0f);
    const rt::PixelResult r = rig.shade({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f});
    EXPECT_FLOAT_EQ(r.rgb.x, 0.2f);
    EXPECT_FLOAT_EQ(r.rgb.y, 0.4f);
    EXPECT_FLOAT_EQ(r.rgb.z, 0.6f);
}

}  // namespace

/// @file test_render_pixel.cpp
/// @brief render_pixel with BruteQuery and NormalShading on the known_two_spheres scene.
#include "shade/render_pixel.hpp"
// Other headers follow, so this file also checks that render_pixel.hpp compiles on its own.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "scene/scene_io.hpp"
#include "trace/brute.hpp"

namespace {

/// Loads scenes/known/known_two_spheres.json once for every test.
class RenderPixel : public testing::Test {
   protected:
    static void SetUpTestSuite() {
        host = new rt::SceneHost(
            rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/known_two_spheres.json"));
    }
    static void TearDownTestSuite() {
        delete host;
        host = nullptr;
    }

    /// Renders one pixel with the policies the first CPU and GPU images use.
    static rt::PixelResult pixel(uint32_t px, uint32_t py) {
        const rt::Scene s = host->view();
        return rt::render_pixel<rt::BruteQuery, rt::NormalShading>(host->camera, px, py, s,
                                                                   rt::BruteQuery{s});
    }

    static rt::SceneHost* host;
};

rt::SceneHost* RenderPixel::host = nullptr;

// Pixels chosen at the 640x480 known camera. Each lies inside a 5x5 block of pixels that all hit
// the same primitive, so rounding differences can't move it to a neighbor.
constexpr uint32_t kSkyX = 320, kSkyY = 10;           // above the horizon: miss
constexpr uint32_t kSphere0X = 232, kSphere0Y = 239;  // center of sphere 0 (left, red)
constexpr uint32_t kSphere1X = 406, kSphere1Y = 239;  // center of sphere 1 (right, blue)
constexpr uint32_t kFloor0X = 320, kFloor0Y = 470;    // floor triangle 0, near the camera
constexpr uint32_t kFloor1X = 440, kFloor1Y = 300;    // floor triangle 1, right of center

/// A miss reports the background, RT_MISS_PRIM and no lights.
TEST_F(RenderPixel, MissPixelIsBackground) {
    const rt::PixelResult p = pixel(kSkyX, kSkyY);
    EXPECT_EQ(p.prim_id, rt::RT_MISS_PRIM);
    EXPECT_EQ(p.light_mask, 0u);
    EXPECT_EQ(p.rgb.x, host->background.x);
    EXPECT_EQ(p.rgb.y, host->background.y);
    EXPECT_EQ(p.rgb.z, host->background.z);
}

/// Each sphere's center pixel hits that sphere, with a normal facing the camera.
TEST_F(RenderPixel, SphereCenterPixelsHitTheirSphere) {
    const rt::PixelResult left = pixel(kSphere0X, kSphere0Y);
    const rt::PixelResult right = pixel(kSphere1X, kSphere1Y);
    EXPECT_EQ(left.prim_id, 0u);
    EXPECT_EQ(right.prim_id, 1u);
    for (const rt::PixelResult& p : {left, right}) {
        EXPECT_EQ(p.light_mask, 0u);
        // NormalShading maps n to 0.5 * (n + 1). The camera looks along about -z, so the visible
        // normal at the sphere's center points mostly toward +z.
        EXPECT_GT(p.rgb.z, 0.9f);
    }
    // The scene is mirror-symmetric in x, so the two normals mirror each other in x.
    EXPECT_NEAR(left.rgb.x, 1.0f - right.rgb.x, 1.0e-2f);
}

/// Floor pixels hit the two floor triangles; the floor's normal is exactly +y.
TEST_F(RenderPixel, FloorPixelsHitTriangles) {
    const rt::PixelResult near = pixel(kFloor0X, kFloor0Y);
    const rt::PixelResult right = pixel(kFloor1X, kFloor1Y);
    EXPECT_EQ(near.prim_id, rt::PRIM_TRIANGLE_BIT | 0u);
    EXPECT_EQ(right.prim_id, rt::PRIM_TRIANGLE_BIT | 1u);
    for (const rt::PixelResult& p : {near, right}) {
        EXPECT_EQ(p.rgb.x, 0.5f);
        EXPECT_EQ(p.rgb.y, 1.0f);
        EXPECT_EQ(p.rgb.z, 0.5f);
        EXPECT_EQ(p.light_mask, 0u);
    }
}

/// render_pixel is exactly camera_ray, then q.closest, then the shading policy, at every pixel.
TEST_F(RenderPixel, MatchesHandComposedPipelineBitForBit) {
    const rt::Scene s = host->view();
    const rt::BruteQuery q{s};
    const rt::Camera& cam = host->camera;
    uint32_t hits = 0;
    for (uint32_t py = 0; py < cam.height; ++py) {
        for (uint32_t px = 0; px < cam.width; ++px) {
            const rt::PixelResult got = rt::render_pixel(cam, px, py, s, q, rt::NormalShading{});
            const rt::Ray r = rt::camera_ray(cam, px, py);
            const rt::PixelResult want = rt::NormalShading{}(r, rt::brute_closest(r, s), s, q);
            ASSERT_EQ(0, std::memcmp(&got, &want, sizeof(got))) << "pixel " << px << "," << py;
            hits += got.prim_id != rt::RT_MISS_PRIM;
        }
    }
    // Both spheres and the floor are in view; the sky fills the rest.
    EXPECT_GT(hits, cam.width * cam.height / 2);
}

}  // namespace

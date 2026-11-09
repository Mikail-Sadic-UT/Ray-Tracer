/// @file test_cpu.cpp
/// @brief CPU image correctness, edge tiles, output reuse and thread determinism.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <string>

#include "core/error.hpp"
#include "cpu/normal_renderer.hpp"
#include "scene/scene_io.hpp"
#include "shade/render_pixel.hpp"
#include "trace/brute.hpp"

namespace {
rt::SceneHost known_scene() {
    return rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/known_two_spheres.json");
}
}  // namespace

/// Every output pixel agrees with the shared implementation, including incomplete tiles.
TEST(CpuNormal, MatchesEveryPixelAndAllThreads) {
    auto scene = known_scene();
    scene.camera.width = 67;
    scene.camera.height = 49;
    rt::Image one, many;
    rt::Timings timings;
    rt::render_normal(scene, one, {1, true, true}, timings);
    EXPECT_GE(timings.render, 0);
    rt::render_normal(scene, many, {4, true, true}, timings);
    ASSERT_EQ(one.rgb.size(), many.rgb.size());
    EXPECT_EQ(std::memcmp(one.rgb.data(), many.rgb.data(), one.rgb.size() * sizeof(float)), 0);
    EXPECT_EQ(one.prim_id, many.prim_id);
    EXPECT_EQ(one.light_mask, many.light_mask);
    const auto view = scene.view();
    for (uint32_t y = 0; y < scene.camera.height; ++y) {
        for (uint32_t x = 0; x < scene.camera.width; ++x) {
            const auto pixel = rt::render_pixel<rt::BruteQuery, rt::NormalShading>(
                scene.camera, x, y, view, rt::BruteQuery{view});
            const size_t i = static_cast<size_t>(y) * scene.camera.width + x;
            EXPECT_EQ(one.rgb[i * 3], pixel.rgb.x);
            EXPECT_EQ(one.rgb[i * 3 + 1], pixel.rgb.y);
            EXPECT_EQ(one.rgb[i * 3 + 2], pixel.rgb.z);
            EXPECT_EQ(one.prim_id[i], pixel.prim_id);
            EXPECT_EQ(one.light_mask[i], 0u);
            EXPECT_TRUE(std::isfinite(one.rgb[i * 3]));
        }
    }
}

/// Known pixels test misses, sphere IDs and the floor; unrequested AOVs are cleared on reuse.
TEST(CpuNormal, KnownPixelsAndOutputReuse) {
    auto scene = known_scene();
    rt::Image image;
    rt::Timings timings;
    rt::render_normal(scene, image, {2, true, true}, timings);
    EXPECT_EQ(image.prim_id[10 * 640 + 320], rt::RT_MISS_PRIM);
    EXPECT_EQ(image.prim_id[239 * 640 + 232], 0u);
    EXPECT_EQ(image.prim_id[239 * 640 + 406], 1u);
    EXPECT_EQ(image.prim_id[470 * 640 + 320], rt::PRIM_TRIANGLE_BIT);
    scene.camera.width = 1;
    scene.camera.height = 1;
    rt::render_normal(scene, image, {}, timings);
    EXPECT_EQ(image.rgb.size(), 3u);
    EXPECT_TRUE(image.prim_id.empty());
    EXPECT_TRUE(image.light_mask.empty());
    EXPECT_THROW(rt::render_normal(scene, image, {0, false, false}, timings), rt::Error);
}

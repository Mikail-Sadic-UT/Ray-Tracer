/// @file test_generator.cpp
/// @brief Official PCG reference vector, generator invariants, fitted bounds and determinism.
#include <gtest/gtest.h>

#include <cmath>
#include <nlohmann/json.hpp>

#include "core/error.hpp"
#include "scene/generator.hpp"
#include "scene/hash.hpp"
#include "scene/pcg32.hpp"
#include "scene/scene_io.hpp"

/// PCG's published demo sequence for seed 42 and stream 54.
TEST(Generator, PcgReferenceVector) {
    rt::Pcg32 random(42, 54);
    for (const uint32_t expected :
         {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu})
        EXPECT_EQ(random.next(), expected);
    for (int i = 0; i < 1000; ++i) {
        const float value = random.uniform();
        EXPECT_GE(value, 0);
        EXPECT_LT(value, 1);
        const auto normal = random.normal_pair();
        EXPECT_TRUE(std::isfinite(normal[0]) && std::isfinite(normal[1]));
    }
}

/// Exact counts, valid materials/radii, seed repeatability and 95% camera framing.
TEST(Generator, UniformCountsRepeatabilityAndFraming) {
    for (const uint32_t count : {4u, 17u, 10000u}) {
        const auto scene = rt::generate_uniform({count, 1});
        const auto again = rt::generate_uniform({count, 1});
        EXPECT_EQ(scene.spheres.size() + scene.triangles.size(), count);
        EXPECT_EQ(scene.hash, again.hash);
        EXPECT_EQ(rt::scene_to_json(scene), rt::scene_to_json(again));
        EXPECT_NO_THROW(rt::validate_scene(scene));
        const float side = 400.0f * std::sqrt(static_cast<float>(count) / 1000000.0f);
        for (size_t i = 0; i < scene.spheres.size(); ++i) {
            const auto& sphere = scene.spheres[i];
            EXPECT_GE(sphere.radius, 0.1f);
            EXPECT_LE(sphere.radius, 1.0f);
            EXPECT_LE(std::abs(sphere.center.x), side * 0.5f);
            EXPECT_LE(std::abs(sphere.center.z), side * 0.5f);
            EXPECT_EQ(sphere.material_id, i % 8);
        }
        const auto bounds = rt::scene_bounds(scene);
        const auto& camera = scene.camera;
        const auto w = rt::normalize(camera.position - camera.look_at);
        const auto u = rt::normalize(rt::cross(camera.up, w)), v = rt::cross(w, u);
        for (unsigned corner = 0; corner < 8; ++corner) {
            const rt::vec3 p{corner & 1u ? bounds.hi.x : bounds.lo.x,
                             corner & 2u ? bounds.hi.y : bounds.lo.y,
                             corner & 4u ? bounds.hi.z : bounds.lo.z};
            const auto relative = p - camera.position;
            const float depth = -rt::dot(relative, w);
            ASSERT_GT(depth, 0);
            const float half_height = std::tan(45.0f * 3.14159265358979323846f / 360.0f);
            EXPECT_LE(std::abs(rt::dot(relative, u)) / (depth * half_height * 1920 / 1080),
                      0.950001f);
            EXPECT_LE(std::abs(rt::dot(relative, v)) / (depth * half_height), 0.950001f);
        }
    }
    EXPECT_NE(rt::generate_uniform({17, 1}).hash, rt::generate_uniform({17, 2}).hash);
    EXPECT_THROW(rt::generate_uniform({0, 1}), rt::Error);
}

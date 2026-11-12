/// @file test_generator.cpp
/// @brief Official PCG reference vector, generator invariants, fitted bounds and determinism.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

#include "core/error.hpp"
#include "scene/generator.hpp"
#include "scene/hash.hpp"
#include "scene/pcg32.hpp"
#include "scene/scene_io.hpp"
#include "scene/terrain.hpp"

/// Analytic samples fix amplitude, wavelength, phase and the asymmetric x/z dependence.
TEST(Terrain, AnalyticHeight) {
    constexpr float pi = 3.14159265358979323846f;
    EXPECT_NEAR(rt::terrain_height(0, 0), 0.5f * std::sin(1.0f), 1e-6f);
    EXPECT_NEAR(rt::terrain_height(10, 0), 2.0f + 0.5f * std::sin(20 * pi / 9 + 1), 1e-6f);
    EXPECT_NEAR(rt::terrain_height(10, 20), -2.0f + 0.5f * std::sin(20 * pi / 9 + 1), 1e-6f);
    EXPECT_NEAR(rt::terrain_height(-10, 0), -2.0f + 0.5f * std::sin(-20 * pi / 9 + 1), 1e-6f);
    for (int z = -200; z <= 200; ++z)
        for (int x = -200; x <= 200; ++x) {
            const float h = rt::terrain_height(static_cast<float>(x), static_cast<float>(z));
            ASSERT_TRUE(std::isfinite(h));
            ASSERT_LE(std::abs(h), 2.5f);
        }
}

/// Every cell covers its square exactly once with shared vertices and upward normals.
TEST(Terrain, GridTopologyAndMaterials) {
    for (const uint32_t g : {1u, 2u, 7u}) {
        constexpr float side = 40.0f;
        const auto triangles = rt::terrain_grid(g, side, 5);
        ASSERT_EQ(triangles.size(), 2u * g * g);
        float area = 0;
        for (uint32_t z = 0; z < g; ++z) {
            for (uint32_t x = 0; x < g; ++x) {
                const size_t index = 2 * (z * g + x);
                const auto& first = triangles[index];
                const auto& second = triangles[index + 1];
                const auto expect_point = [&](rt::vec3 p, uint32_t i, uint32_t j) {
                    EXPECT_FLOAT_EQ(p.x, -20.0f + side * static_cast<float>(i) / g);
                    EXPECT_FLOAT_EQ(p.z, -20.0f + side * static_cast<float>(j) / g);
                    EXPECT_FLOAT_EQ(p.y, rt::terrain_height(p.x, p.z));
                };
                expect_point(first.v0, x, z);
                expect_point(first.v1, x, z + 1);
                expect_point(first.v2, x + 1, z + 1);
                expect_point(second.v0, x, z);
                expect_point(second.v1, x + 1, z + 1);
                expect_point(second.v2, x + 1, z);
                for (const size_t i : {index, index + 1}) {
                    const auto& t = triangles[i];
                    const auto normal = rt::cross(t.v1 - t.v0, t.v2 - t.v0);
                    EXPECT_GT(normal.y, 0);
                    area += 0.5f * normal.y;
                    EXPECT_EQ(t.material_id, (5u + i) % 8u);
                }
            }
        }
        EXPECT_NEAR(area, side * side, 0.01f);
    }
}

/// Invalid and overflowing requests fail before allocating or evaluating grid coordinates.
TEST(Terrain, RejectInvalidGrid) {
    EXPECT_THROW(rt::terrain_grid(0, 40, 0), rt::Error);
    for (float side : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN()})
        EXPECT_THROW(rt::terrain_grid(1, side, 0), rt::Error);
    EXPECT_THROW(rt::terrain_grid(32768, 40, 0), rt::Error);
    EXPECT_THROW(rt::terrain_grid(std::numeric_limits<uint32_t>::max(), 40, 0), rt::Error);
    EXPECT_THROW(rt::terrain_grid(1, 40, 0x7ffffffeu), rt::Error);
}

/// Seeds change sphere placement while preserving the grid; spheres sit above the height field.
TEST(Terrain, GeneratorIntegration) {
    const auto a = rt::generate_uniform({10000, 1});
    const auto b = rt::generate_uniform({10000, 42});
    ASSERT_EQ(a.triangles.size(), 5000u);
    ASSERT_EQ(b.triangles.size(), a.triangles.size());
    for (size_t i = 0; i < a.triangles.size(); ++i) {
        const auto& t = a.triangles[i];
        const auto& other = b.triangles[i];
        for (int axis = 0; axis < 3; ++axis) {
            EXPECT_EQ(t.v0[axis], other.v0[axis]);
            EXPECT_EQ(t.v1[axis], other.v1[axis]);
            EXPECT_EQ(t.v2[axis], other.v2[axis]);
        }
        EXPECT_EQ(t.material_id, (a.spheres.size() + i) % 8u);
    }
    for (const auto& sphere : a.spheres) {
        const float offset =
            sphere.center.y - sphere.radius - rt::terrain_height(sphere.center.x, sphere.center.z);
        EXPECT_GE(offset, -1e-6f);
        EXPECT_LE(offset, 1.000001f);
    }
}

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

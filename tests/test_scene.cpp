/// @file test_scene.cpp
/// @brief Checks host scene ownership and the non-owning view contract.
#include <gtest/gtest.h>

#include "scene/scene_host.hpp"

/// Empty arrays must expose null pointers, including after retained storage is cleared.
TEST(SceneHost, EmptyArraysHaveNullPointers) {
    rt::SceneHost host;
    host.spheres.reserve(2);
    host.triangles.resize(1);
    host.triangles.clear();
    host.materials.reserve(2);
    host.lights.resize(1);
    host.lights.clear();

    const rt::Scene scene = host.view();
    EXPECT_EQ(scene.spheres, nullptr);
    EXPECT_EQ(scene.num_spheres, 0u);
    EXPECT_EQ(scene.triangles, nullptr);
    EXPECT_EQ(scene.num_triangles, 0u);
    EXPECT_EQ(scene.materials, nullptr);
    EXPECT_EQ(scene.num_materials, 0u);
    EXPECT_EQ(scene.lights, nullptr);
    EXPECT_EQ(scene.num_lights, 0u);
}

/// Views reference each owned array directly and retain its distinct count.
TEST(SceneHost, PopulatedViewSharesOwnedStorage) {
    rt::SceneHost host;
    host.spheres.resize(2);
    host.triangles.resize(3);
    host.materials.resize(4);
    host.lights.resize(5);
    const rt::SceneHost& owner = host;

    const rt::Scene scene = owner.view();
    EXPECT_EQ(scene.spheres, owner.spheres.data());
    EXPECT_EQ(scene.num_spheres, 2u);
    EXPECT_EQ(scene.triangles, owner.triangles.data());
    EXPECT_EQ(scene.num_triangles, 3u);
    EXPECT_EQ(scene.materials, owner.materials.data());
    EXPECT_EQ(scene.num_materials, 4u);
    EXPECT_EQ(scene.lights, owner.lights.data());
    EXPECT_EQ(scene.num_lights, 5u);
}

/// Global shading values are copied into the view without changing the owner.
TEST(SceneHost, ViewPreservesBackgroundAndAmbient) {
    rt::SceneHost host;
    host.background = {0.125f, 0.25f, 0.5f};
    host.ambient = 0.0625f;
    const rt::Scene scene = host.view();
    EXPECT_EQ(scene.background.x, 0.125f);
    EXPECT_EQ(scene.background.y, 0.25f);
    EXPECT_EQ(scene.background.z, 0.5f);
    EXPECT_EQ(scene.ambient, 0.0625f);
}

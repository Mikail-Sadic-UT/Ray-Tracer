/// @file known_two_spheres_data.hpp
/// @brief scenes/known/known_two_spheres.json as plain C++ values, for tests that can't load JSON.
///
/// CUDA test files can't include the scene reader (it pulls nlohmann/json into nvcc), so the
/// device test uses these values. test_render_pixel.cpp checks that load_scene() on the JSON
/// returns exactly these values, so editing the JSON without updating this file fails a test.
/// Materials and lights are omitted: normal shading doesn't use them.
#pragma once

#include <vector>

#include "camera/camera.hpp"
#include "scene/scene.hpp"

namespace known_two_spheres {

inline const std::vector<rt::Sphere> kSpheres{{{-0.75f, 0.5f, 0.0f}, 0.5f, 0},
                                              {{0.75f, 0.5f, 0.0f}, 0.5f, 1}};
inline const std::vector<rt::Triangle> kTriangles{
    {{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 2},
    {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 2}};
inline const rt::Camera kCamera{
    {0.0f, 1.0f, 5.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f, 640, 480};
inline const rt::color kBackground{0.1f, 0.1f, 0.15f};
inline const float kAmbient = 0.05f;

}  // namespace known_two_spheres

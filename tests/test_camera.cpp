/// @file test_camera.cpp
/// @brief Tests for perspective camera ray generation.
#include <cmath>

#include <gtest/gtest.h>

#include "camera/camera.hpp"

namespace {

rt::Camera make_camera(uint32_t width = 101, uint32_t height = 51) {
    rt::Camera camera;
    camera.position = rt::vec3{0.0f, 0.0f, 5.0f};
    camera.look_at = rt::vec3{0.0f, 0.0f, 0.0f};
    camera.up = rt::vec3{0.0f, 1.0f, 0.0f};
    camera.vfov_deg = 45.0f;
    camera.width = width;
    camera.height = height;
    return camera;
}

float dot_func(const rt::vec3& a, const rt::vec3& b) {
    return rt::dot(a, b);
}

void expect_direction(const rt::vec3& actual, const rt::vec3& expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

}  // namespace

/// Checks that the center pixel ray points at the look-at target.
TEST(CameraRayTest, CenterRayHitsLookAt) {
    rt::Camera camera = make_camera();
    rt::Ray ray = rt::camera_ray(camera, camera.width / 2, camera.height / 2);

    rt::vec3 expected = rt::normalize(camera.look_at - camera.position);
    expect_direction(ray.dir, expected);
    expect_direction(ray.origin, camera.position);
    EXPECT_NEAR(rt::length(ray.dir), 1.0f, 1e-5f);
    EXPECT_FLOAT_EQ(ray.tmin, 0.0f);
    EXPECT_TRUE(std::isinf(ray.tmax) && ray.tmax > 0.0f);
}

/// Checks that every corner direction matches the configured FOV and aspect ratio.
TEST(CameraRayTest, CornerDirectionsMatchFovAndAspect) {
    rt::Camera camera = make_camera(100, 50);

    const float tan_v = tanf(camera.vfov_deg * 3.14159265358979323846f / 360.0f);
    const float tan_h = (static_cast<float>(camera.width) /
                         static_cast<float>(camera.height)) * tan_v;
    const rt::vec3 w = rt::normalize(camera.position - camera.look_at);
    const rt::vec3 u = rt::normalize(rt::cross(camera.up, w));
    const rt::vec3 v = rt::cross(w, u);

    const uint32_t xs[] = {0, camera.width - 1};
    const uint32_t ys[] = {0, camera.height - 1};
    for (uint32_t py : ys) {
        const float sy = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) /
                                    static_cast<float>(camera.height);
        for (uint32_t px : xs) {
            const float sx = 2.0f * (static_cast<float>(px) + 0.5f) /
                             static_cast<float>(camera.width) - 1.0f;
            const rt::vec3 expected = rt::normalize(-w + sx * tan_h * u + sy * tan_v * v);
            rt::Ray ray = rt::camera_ray(camera, px, py);
            expect_direction(ray.dir, expected);
            EXPECT_NEAR(rt::length(ray.dir), 1.0f, 1e-5f);
        }
    }
}

/// Checks that the same x-coordinate is positive in row zero versus negative in the bottom row.
TEST(CameraRayTest, RowZeroIsTop) {
    rt::Camera camera = make_camera();
    rt::Ray top = rt::camera_ray(camera, camera.width / 2, 0);
    rt::Ray bottom = rt::camera_ray(camera, camera.width / 2, camera.height - 1);

    const rt::vec3 w = rt::normalize(camera.position - camera.look_at);
    const rt::vec3 v = rt::cross(w, rt::normalize(rt::cross(camera.up, w)));

    EXPECT_GT(dot_func(top.dir, v), dot(bottom.dir, v));
    EXPECT_GT(dot_func(top.dir, v), 0.0f);
    EXPECT_LT(dot_func(bottom.dir, v), 0.0f);
}

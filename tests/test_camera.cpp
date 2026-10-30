/// @file test_camera.cpp
/// @brief Tests for perspective camera ray generation.
#include <gtest/gtest.h>

#include <cmath>

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
    rt::Camera camera = make_camera(2, 2);
    camera.vfov_deg = 90.0f;

    // At 90 degrees, the pixel centers lie at (+/-0.5, +/-0.5, -1).
    // Their length is sqrt(1.5), giving these hand-computed unit vectors.
    const rt::vec3 expected[] = {
        {-0.408248f, 0.408248f, -0.816497f},
        {0.408248f, 0.408248f, -0.816497f},
        {-0.408248f, -0.408248f, -0.816497f},
        {0.408248f, -0.408248f, -0.816497f},
    };
    for (uint32_t py = 0; py < camera.height; ++py) {
        for (uint32_t px = 0; px < camera.width; ++px) {
            SCOPED_TRACE(::testing::Message() << "pixel (" << px << ", " << py << ")");
            const rt::Ray ray = rt::camera_ray(camera, px, py);
            expect_direction(ray.dir, expected[py * camera.width + px]);
            EXPECT_NEAR(rt::length(ray.dir), 1.0f, 1e-5f);
        }
    }
}

/// Checks a non-square image against hand-computed corner directions.
TEST(CameraRayTest, WideImageCornersMatchAspectRatio) {
    rt::Camera camera = make_camera(4, 2);
    camera.vfov_deg = 90.0f;

    // Aspect 2 puts the corner pixel centers at (+/-1.5, +/-0.5, -1).
    // Dividing by sqrt(3.5) gives these directions.
    expect_direction(rt::camera_ray(camera, 0, 0).dir, {-0.801784f, 0.267261f, -0.534522f});
    expect_direction(rt::camera_ray(camera, 3, 0).dir, {0.801784f, 0.267261f, -0.534522f});
    expect_direction(rt::camera_ray(camera, 0, 1).dir, {-0.801784f, -0.267261f, -0.534522f});
    expect_direction(rt::camera_ray(camera, 3, 1).dir, {0.801784f, -0.267261f, -0.534522f});
}

/// Checks that row zero points up and the bottom row points down.
TEST(CameraRayTest, RowZeroIsTop) {
    rt::Camera camera = make_camera();
    rt::Ray top = rt::camera_ray(camera, camera.width / 2, 0);
    rt::Ray bottom = rt::camera_ray(camera, camera.width / 2, camera.height - 1);

    EXPECT_GT(rt::dot(top.dir, camera.up), rt::dot(bottom.dir, camera.up));
    EXPECT_GT(rt::dot(top.dir, camera.up), 0.0f);
    EXPECT_LT(rt::dot(bottom.dir, camera.up), 0.0f);
}

/// Checks that the left and right image edges point to the corresponding side.
TEST(CameraRayTest, LeftAndRightEdgesPointOutward) {
    const rt::Camera camera = make_camera();
    const rt::Ray left = rt::camera_ray(camera, 0, camera.height / 2);
    const rt::Ray right = rt::camera_ray(camera, camera.width - 1, camera.height / 2);

    EXPECT_LT(left.dir.x, 0.0f);
    EXPECT_GT(right.dir.x, 0.0f);
}

/// Checks a translated, rotated camera with a non-unit, non-orthogonal up vector.
TEST(CameraRayTest, OffAxisCenterRayHitsLookAtAndAllRaysAreUnitLength) {
    rt::Camera camera = make_camera(7, 5);
    camera.position = {3.0f, 2.0f, 1.0f};
    camera.look_at = {-1.0f, 0.5f, 4.0f};
    camera.up = {0.2f, 3.0f, 0.1f};

    // Target displacement (-4, -1.5, 3) has length sqrt(27.25).
    const rt::Ray center = rt::camera_ray(camera, camera.width / 2, camera.height / 2);
    expect_direction(center.dir, {-0.766261f, -0.287348f, 0.574696f});

    for (uint32_t py = 0; py < camera.height; ++py) {
        for (uint32_t px = 0; px < camera.width; ++px) {
            SCOPED_TRACE(::testing::Message() << "pixel (" << px << ", " << py << ")");
            const rt::Ray ray = rt::camera_ray(camera, px, py);
            expect_direction(ray.origin, camera.position);
            EXPECT_NEAR(rt::length(ray.dir), 1.0f, 1e-5f);
        }
    }
}

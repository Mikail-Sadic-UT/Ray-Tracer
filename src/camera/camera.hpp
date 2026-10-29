/// @file camera.hpp
/// @brief Camera ray generation and camera data.
#pragma once

#include <cmath>
#include <cstdint>

#include "core/math.hpp"
#include "core/ray.hpp"

namespace rt {

/// Perspective camera parameters shared by host and device code.
struct Camera {
    vec3 position;    ///< Camera position.
    vec3 look_at;     ///< Point the camera looks toward.
    vec3 up;          ///< Camera's approximate up direction.
    float vfov_deg;   ///< Vertical field of view in degrees.
    uint32_t width;   ///< Image width in pixels.
    uint32_t height;  ///< Image height in pixels.
};

static_assert(sizeof(Camera) == 48, "Camera must be 48 bytes");

/// Generates a normalized ray through the center of pixel `(px, py)`.
/// @param cam Camera parameters.
/// @param px Pixel column.
/// @param py Pixel row, with row zero at the top.
/// @return Ray with `tmin = 0` and `tmax = +inf`.
/// @pre `cam.width` and `cam.height` are nonzero and `position != look_at`.
RT_INLINE Ray camera_ray(const Camera& cam, uint32_t px, uint32_t py) {
    const float pi = 3.14159265358979323846f;
    const float aspect = static_cast<float>(cam.width) / static_cast<float>(cam.height);
    const float viewport_half_height = tanf(cam.vfov_deg * pi / 360.0f);
    const float viewport_half_width = aspect * viewport_half_height;

    const vec3 w = normalize(cam.position - cam.look_at);
    const vec3 u = normalize(cross(cam.up, w));
    const vec3 v = cross(w, u);

    const float sx = 2.0f * (static_cast<float>(px) + 0.5f) / static_cast<float>(cam.width) - 1.0f;
    const float sy = 1.0f - 2.0f * (static_cast<float>(py) + 0.5f) / static_cast<float>(cam.height);

    Ray ray;
    ray.origin = cam.position;
    ray.dir = normalize((-w) + sx * viewport_half_width * u + sy * viewport_half_height * v);
    ray.tmin = 0.0f;
    ray.tmax = INFINITY;
    return ray;
}

}  // namespace rt

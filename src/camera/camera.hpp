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

/// `tan(x)` for finite `0 < x < pi/2`, bit-identical on host and device.
///
/// `tanf` is not: glibc and CUDA round it differently for about a third of all angles (a 70
/// degree field of view is one), which moved every camera ray by an ulp and broke byte-equal CPU
/// and GPU images. This uses only IEEE multiplication, subtraction and division in `double`, in
/// a fixed order: sine and cosine of `x / 8` by Taylor series, three angle doublings, a division
/// and one rounding to float. The double result is within a few 1e-16 of `tan(x)`, so the float
/// is the correctly rounded value except where `tan(x)` is within that of a rounding boundary.
RT_INLINE float tan_det(float x) {
    constexpr double kSin[7] = {1.0 / 210.0, 1.0 / 156.0, 1.0 / 110.0, 1.0 / 72.0,
                                1.0 / 42.0,  1.0 / 20.0,  1.0 / 6.0};
    constexpr double kCos[7] = {1.0 / 182.0, 1.0 / 132.0, 1.0 / 90.0, 1.0 / 56.0,
                                1.0 / 30.0,  1.0 / 12.0,  1.0 / 2.0};
    const double y = static_cast<double>(x) * 0.125;
    const double y2 = y * y;
    double s = 1.0;
    double c = 1.0;
    for (int i = 0; i < 7; ++i) {
        s = 1.0 - y2 * kSin[i] * s;
        c = 1.0 - y2 * kCos[i] * c;
    }
    s *= y;
    for (int i = 0; i < 3; ++i) {
        const double sn = 2.0 * s * c;
        const double cn = (c - s) * (c + s);
        s = sn;
        c = cn;
    }
    return static_cast<float>(s / c);
}

/// Generates a normalized ray through the center of pixel `(px, py)`.
/// @param cam Camera parameters.
/// @param px Pixel column.
/// @param py Pixel row, with row zero at the top.
/// @return Ray with `tmin = 0` and `tmax = +inf`.
/// @pre `cam.width` and `cam.height` are nonzero and `position != look_at`.
RT_INLINE Ray camera_ray(const Camera& cam, uint32_t px, uint32_t py) {
    const float pi = 3.14159265358979323846f;
    const float aspect = static_cast<float>(cam.width) / static_cast<float>(cam.height);
    const float viewport_half_height = tan_det(cam.vfov_deg * pi / 360.0f);
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

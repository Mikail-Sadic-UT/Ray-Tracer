/// @file camera_stub.hpp
/// @brief Temporary camera storage for SceneHost until Jett's camera header lands.
#pragma once

#include <cstdint>

#include "core/math.hpp"

namespace rt {

// STUB(S1, po): remove when camera/camera.hpp lands (C3, docs/members/JETT.md).
/// Field-for-field stand-in for INTERFACES section 4; provides no ray generation.
struct CameraStub {
    vec3 position;    ///< World-space eye position.
    vec3 look_at;     ///< World-space target.
    vec3 up;          ///< Camera up vector.
    float vfov_deg;   ///< Vertical field of view in degrees.
    uint32_t width;   ///< Image width in pixels.
    uint32_t height;  ///< Image height in pixels.
};

static_assert(sizeof(CameraStub) == 48, "Camera storage must be 48 bytes");

}  // namespace rt

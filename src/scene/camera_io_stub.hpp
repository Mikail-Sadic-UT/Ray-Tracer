/// @file camera_io_stub.hpp
/// @brief Temporary host camera parsing and validation until Jett's camera IO lands.
#pragma once

#include <cmath>

#include "camera/camera.hpp"
#include "scene/json_helpers.hpp"

namespace rt::detail {

// STUB(S2, po): remove when Jett's camera parsing and validation land (C3, docs/ROADMAP.md).
/// Reads the camera fields from INTERFACES section 4 without inventing defaults.
inline Camera parse_camera_stub(const nlohmann::json& value) {
    return {read_vec3(member(value, "position", "camera"), "camera.position"),
            read_vec3(member(value, "look_at", "camera"), "camera.look_at"),
            read_vec3(member(value, "up", "camera"), "camera.up"),
            read_float(member(value, "vfov_deg", "camera"), "camera.vfov_deg"),
            read_u32(member(value, "width", "camera"), "camera.width"),
            read_u32(member(value, "height", "camera"), "camera.height")};
}

/// Encodes the temporary camera storage without changing float values.
inline nlohmann::json camera_to_json_stub(const Camera& camera) {
    return {{"position", {camera.position.x, camera.position.y, camera.position.z}},
            {"look_at", {camera.look_at.x, camera.look_at.y, camera.look_at.z}},
            {"up", {camera.up.x, camera.up.y, camera.up.z}},
            {"vfov_deg", camera.vfov_deg},
            {"width", camera.width},
            {"height", camera.height}};
}

/// Checks the contract's camera and resolution rules, also after caller-side overrides.
inline void validate_camera_stub(const Camera& camera) {
    finite_vec3(camera.position, "camera.position");
    finite_vec3(camera.look_at, "camera.look_at");
    finite_vec3(camera.up, "camera.up");
    finite_float(camera.vfov_deg, "camera.vfov_deg");
    if (!(camera.vfov_deg > 0.0f && camera.vfov_deg < 180.0f))
        scene_error("camera.vfov_deg", "must be between 0 and 180 degrees, exclusively");
    if (camera.width < 1 || camera.width > 16384)
        scene_error("camera.width", "must be in [1, 16384]");
    if (camera.height < 1 || camera.height > 16384)
        scene_error("camera.height", "must be in [1, 16384]");

    // Double intermediates avoid overflow/underflow when checking finite float vectors.
    double w[3], up[3];
    double w2 = 0.0, up2 = 0.0;
    for (int i = 0; i < 3; ++i) {
        w[i] = static_cast<double>(camera.position[i]) - camera.look_at[i];
        up[i] = camera.up[i];
        w2 += w[i] * w[i];
        up2 += up[i] * up[i];
    }
    if (w2 == 0.0) scene_error("camera.look_at", "must differ from camera.position");
    if (up2 == 0.0) scene_error("camera.up", "must be nonzero");
    for (int i = 0; i < 3; ++i) {
        w[i] /= std::sqrt(w2);
        up[i] /= std::sqrt(up2);
    }
    const double x = up[1] * w[2] - up[2] * w[1];
    const double y = up[2] * w[0] - up[0] * w[2];
    const double z = up[0] * w[1] - up[1] * w[0];
    if (std::sqrt(x * x + y * y + z * z) < 1.0e-6)
        scene_error("camera.up", "must not be parallel or nearly parallel to the view direction");
}

}  // namespace rt::detail

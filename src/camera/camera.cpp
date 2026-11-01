/// @file camera.cpp
/// @brief Host-only camera support: JSON parsing, encoding, and validation.
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "camera/camera_io.hpp"
#include "core/error.hpp"

namespace rt {
namespace {

using nlohmann::json;

/// Rejects invalid camera input with the offending field path.
[[noreturn]] void camera_error(const std::string& field, const std::string& reason) {
    throw Error("camera: " + field + ": " + reason);
}

/// Requires a member of the camera object instead of supplying a default.
const json& member(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) camera_error(std::string("camera.") + key, "missing field");
    return *it;
}

/// Reads a finite float without overflowing a double-to-float conversion.
float read_float(const json& value, const std::string& path) {
    if (!value.is_number()) camera_error(path, "expected a number");
    const double number = value.get<double>();
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(number) || number < -limit || number > limit)
        camera_error(path, "expected a finite float");
    return static_cast<float>(number);
}

/// Reads a uint32 without accepting fractions, negative integers, or wrapping overflow.
uint32_t read_u32(const json& value, const std::string& path) {
    if (!value.is_number_integer()) camera_error(path, "expected an unsigned integer");
    if (!value.is_number_unsigned() && value.get<int64_t>() < 0)
        camera_error(path, "expected an unsigned integer");
    const uint64_t number = value.get<uint64_t>();
    if (number > std::numeric_limits<uint32_t>::max())
        camera_error(path, "integer exceeds uint32 range");
    return static_cast<uint32_t>(number);
}

/// Reads exactly three finite float components.
vec3 read_vec3(const json& value, const std::string& path) {
    if (!value.is_array()) camera_error(path, "expected an array");
    if (value.size() != 3) camera_error(path, "expected exactly 3 components");
    return {read_float(value[0], path + "[0]"), read_float(value[1], path + "[1]"),
            read_float(value[2], path + "[2]")};
}

/// Checks each component of a vector set by code or by an override.
void finite_vec3(vec3 value, const std::string& path) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(value[i]))
            camera_error(path + "[" + std::to_string(i) + "]", "must be finite");
}

}  // namespace

Camera parse_camera(const json& value) {
    if (!value.is_object()) camera_error("camera", "expected an object");
    Camera camera;
    camera.position = read_vec3(member(value, "position"), "camera.position");
    camera.look_at = read_vec3(member(value, "look_at"), "camera.look_at");
    camera.up = read_vec3(member(value, "up"), "camera.up");
    camera.vfov_deg = read_float(member(value, "vfov_deg"), "camera.vfov_deg");
    camera.width = read_u32(member(value, "width"), "camera.width");
    camera.height = read_u32(member(value, "height"), "camera.height");
    return camera;
}

json camera_to_json(const Camera& camera) {
    const auto vector = [](vec3 v) { return json::array({v.x, v.y, v.z}); };
    return {{"position", vector(camera.position)},
            {"look_at", vector(camera.look_at)},
            {"up", vector(camera.up)},
            {"vfov_deg", camera.vfov_deg},
            {"width", camera.width},
            {"height", camera.height}};
}

void validate_camera(const Camera& camera) {
    finite_vec3(camera.position, "camera.position");
    finite_vec3(camera.look_at, "camera.look_at");
    finite_vec3(camera.up, "camera.up");
    if (!std::isfinite(camera.vfov_deg)) camera_error("camera.vfov_deg", "must be finite");
    if (!(camera.vfov_deg > 0.0f && camera.vfov_deg < 180.0f))
        camera_error("camera.vfov_deg", "must be between 0 and 180 degrees, exclusively");
    if (camera.width < 1 || camera.width > 16384)
        camera_error("camera.width", "must be in [1, 16384]");
    if (camera.height < 1 || camera.height > 16384)
        camera_error("camera.height", "must be in [1, 16384]");

    // Double intermediates: squaring finite floats can overflow or underflow in float, and the
    // threshold below must not depend on float rounding (CONTRACT_NOTES W2).
    double w[3], up[3];
    double w2 = 0.0, up2 = 0.0;
    for (int i = 0; i < 3; ++i) {
        w[i] = static_cast<double>(camera.position[i]) - camera.look_at[i];
        up[i] = camera.up[i];
        w2 += w[i] * w[i];
        up2 += up[i] * up[i];
    }
    if (w2 == 0.0) camera_error("camera.look_at", "must differ from camera.position");
    if (up2 == 0.0) camera_error("camera.up", "must be nonzero");
    const double w_len = std::sqrt(w2), up_len = std::sqrt(up2);
    for (int i = 0; i < 3; ++i) {
        w[i] /= w_len;
        up[i] /= up_len;
    }
    const double x = up[1] * w[2] - up[2] * w[1];
    const double y = up[2] * w[0] - up[0] * w[2];
    const double z = up[0] * w[1] - up[1] * w[0];
    if (std::sqrt(x * x + y * y + z * z) < 1.0e-6)
        camera_error("camera.up", "must not be parallel or nearly parallel to the view direction");
}

}  // namespace rt

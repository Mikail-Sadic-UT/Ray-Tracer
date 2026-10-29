/// @file json_helpers.hpp
/// @brief Internal helpers for strict, field-specific JSON errors. Host-only.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>

#include "core/error.hpp"
#include "core/math.hpp"

namespace rt::detail {

/// Rejects invalid scene input with the offending field path.
[[noreturn]] inline void scene_error(const std::string& field, const std::string& reason) {
    throw Error("scene_io: " + field + ": " + reason);
}

/// Requires an object member instead of silently supplying a default.
inline const nlohmann::json& member(const nlohmann::json& object, const char* key,
                                    const std::string& path) {
    if (!object.is_object()) scene_error(path, "expected an object");
    const auto it = object.find(key);
    if (it == object.end()) scene_error(path.empty() ? key : path + "." + key, "missing field");
    return *it;
}

/// Requires an array, optionally with an exact number of entries.
inline void require_array(const nlohmann::json& value, const std::string& path) {
    if (!value.is_array()) scene_error(path, "expected an array");
}

/// Reads a finite float without overflowing a double-to-float conversion.
inline float read_float(const nlohmann::json& value, const std::string& path) {
    if (!value.is_number()) scene_error(path, "expected a number");
    const double number = value.get<double>();
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(number) || number < -limit || number > limit)
        scene_error(path, "expected a finite float");
    return static_cast<float>(number);
}

/// Reads a uint32 without accepting fractions, negative integers, or wrapping overflow.
inline uint32_t read_u32(const nlohmann::json& value, const std::string& path) {
    if (!value.is_number_integer()) scene_error(path, "expected an unsigned integer");
    if (!value.is_number_unsigned() && value.get<int64_t>() < 0)
        scene_error(path, "expected an unsigned integer");
    const uint64_t number = value.get<uint64_t>();
    if (number > std::numeric_limits<uint32_t>::max())
        scene_error(path, "integer exceeds uint32 range");
    return static_cast<uint32_t>(number);
}

/// Reads exactly three finite float components.
inline vec3 read_vec3(const nlohmann::json& value, const std::string& path) {
    require_array(value, path);
    if (value.size() != 3) scene_error(path, "expected exactly 3 components");
    return {read_float(value[0], path + "[0]"), read_float(value[1], path + "[1]"),
            read_float(value[2], path + "[2]")};
}

/// Checks numeric values in scenes assembled by code as well as JSON loaders.
inline void finite_float(float value, const std::string& path) {
    if (!std::isfinite(value)) scene_error(path, "must be finite");
}

/// Checks a world-space vector component by component.
inline void finite_vec3(vec3 value, const std::string& path) {
    for (int i = 0; i < 3; ++i) finite_float(value[i], path + "[" + std::to_string(i) + "]");
}

/// Checks a linear color or intensity, with no upper bound or clamping.
inline void nonnegative_vec3(vec3 value, const std::string& path) {
    finite_vec3(value, path);
    for (int i = 0; i < 3; ++i)
        if (value[i] < 0.0f)
            scene_error(path + "[" + std::to_string(i) + "]", "must be nonnegative");
}

}  // namespace rt::detail

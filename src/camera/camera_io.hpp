/// @file camera_io.hpp
/// @brief Host-only camera JSON parsing, encoding, and validation.
#pragma once

#include <nlohmann/json.hpp>

#include "camera/camera.hpp"

namespace rt {

/// Reads the `camera` object of a scene file. Every field is required; no defaults are supplied.
/// Does not validate the values; call validate_camera() after any overrides are applied.
/// @param value The JSON value of the scene's `camera` member.
/// @throws Error naming the field (e.g. `camera.width`) if a field is missing or has the wrong type.
Camera parse_camera(const nlohmann::json& value);

/// Encodes a camera as the JSON object parse_camera() reads, preserving every float value.
nlohmann::json camera_to_json(const Camera& camera);

/// Checks the camera and resolution rules from INTERFACES section 2, "Valid input". Call it
/// after loading and again after CLI overrides. The `up`-parallel check uses double precision.
/// @throws Error naming the offending field.
void validate_camera(const Camera& camera);

}  // namespace rt

/// @file scene_io.hpp
/// @brief Host-only inline JSON scene loading and shared load validation.
#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

#include "scene/scene_host.hpp"

namespace rt {

/// Parses version 2 inline or binary geometry, validates it, and computes its scene hash.
/// Binary paths in this in-memory API resolve against the current working directory.
/// @throws Error for invalid schema or scene fields, naming the field and array index.
SceneHost parse_scene(const nlohmann::json& document);

/// Reads an entire JSON file, including rejecting trailing non-whitespace content.
/// @throws Error on file, JSON, or validation errors, including the source path.
SceneHost load_scene(const std::string& path);

/// Encodes a validated scene as version 2 inline JSON, preserving all scene field values.
/// Material names and generator metadata are not stored in SceneHost and are not emitted.
/// @throws Error if any scene field is invalid.
nlohmann::json scene_to_json(const SceneHost& scene);

/// Writes a validated inline JSON scene. Recomputes the hash when the file is loaded again.
/// @throws Error on validation or file write failure.
void save_scene(const std::string& path, const SceneHost& scene);

/// Saves a .json metadata file and a sibling .bin geometry file with matching stem.
/// Binary geometry references in load_scene resolve relative to the JSON file's directory.
/// Optional generator metadata is emitted unchanged, outside the canonical scene hash.
/// @throws Error if the destination is not .json, validation fails, or files cannot be written.
void save_binary_scene(const std::string& path, const SceneHost& scene,
                       const nlohmann::json& generator);

/// Validates scenes assembled by code, or after camera/resolution overrides.
/// Core accepts point lights only. The light-mask flag additionally limits lights to 32.
/// Does not change the scene or update its hash.
/// @throws Error naming the invalid field and index.
void validate_scene(const SceneHost& scene, bool require_light_mask = false);

}  // namespace rt

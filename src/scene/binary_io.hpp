/// @file binary_io.hpp
/// @brief Version 2 little-endian binary geometry IO.
#pragma once

#include <string>

#include "scene/scene_host.hpp"

namespace rt {

/// Decoded geometry, independent of the materials, lights and camera stored in JSON.
struct GeometryData {
    std::vector<Sphere> spheres;
    std::vector<Triangle> triangles;
    uint64_t hash = 0;  ///< Verified canonical geometry hash.
};

/// Reads and verifies the header, offsets, primitive counts, payload size and content hash.
/// Full field and material-index validation is performed by load_scene after combining JSON.
/// @throws Error on invalid files or read failure.
GeometryData read_geometry(const std::string& path);

/// Writes validated geometry field by field with a 64-byte header and 16-byte array alignment.
/// @throws Error on invalid scene input or write failure.
void write_geometry(const std::string& path, const SceneHost& scene);

}  // namespace rt

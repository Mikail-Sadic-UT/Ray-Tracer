/// @file obj_io.hpp
/// @brief Wavefront OBJ triangle meshes for obj2bin (E6): parsing and placement in a scene.
#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

#include "core/math.hpp"
#include "scene/scene_host.hpp"

namespace rt {

/// Vertices and triangles of an OBJ file. Faces index `vertices` from 0.
struct ObjMesh {
    std::vector<vec3> vertices;
    std::vector<std::array<uint32_t, 3>> faces;
};

/// Reads `v` and `f` lines; everything else (normals, texture coordinates, groups, materials,
/// comments) is ignored. Face corners may be `v`, `v/vt`, `v//vn` or `v/vt/vn`, with 1-based
/// or negative (relative) indices. Polygons with more than three corners are split into a fan
/// from the first corner, keeping their winding.
/// @param name Source name used in error messages.
/// @throws Error on a malformed vertex, a face with fewer than three corners, or an index out
///         of range, naming the source and line.
ObjMesh parse_obj(std::istream& input, const std::string& name);

/// Opens @p path and parses it with parse_obj().
/// @throws Error if the file cannot be read, or as parse_obj().
ObjMesh load_obj(const std::string& path);

/// Where a mesh goes in the scene.
struct MeshPlacement {
    float size = 2.0f;            ///< Largest bounding-box extent after scaling.
    float rotate_y_deg = 0.0f;    ///< Rotation about the vertical axis through the mesh center.
    vec3 base{0.0f, 0.0f, 0.0f};  ///< Where the bottom center of the bounding box lands.
    uint32_t material_id = 0u;    ///< Material of every triangle.
};

/// Appends the mesh to `scene.triangles`: scaled so its largest extent is `size`, rotated about
/// +Y, and moved so the center of its bounding box's bottom face is at `base`. Computed in
/// double and rounded once per coordinate. Winding is kept, so counterclockwise faces still
/// face outward (glass relies on it).
/// @throws Error if the mesh has no faces, its vertices are not finite, it is flat in every
///         direction, `size` is not positive, or `material_id` is not a scene material.
void append_mesh(SceneHost& scene, const ObjMesh& mesh, const MeshPlacement& placement);

}  // namespace rt

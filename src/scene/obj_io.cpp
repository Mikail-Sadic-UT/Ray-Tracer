/// @file obj_io.cpp
/// @brief Wavefront OBJ triangle meshes for obj2bin (E6).
#include "scene/obj_io.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "core/error.hpp"

namespace rt {
namespace {

[[noreturn]] void obj_error(const std::string& name, std::size_t line, const std::string& what) {
    throw Error(name + ":" + std::to_string(line) + ": " + what);
}

/// Resolves one face corner (`v`, `v/vt`, `v//vn`, `v/vt/vn`) to a 0-based vertex index.
uint32_t corner_index(const std::string& token, std::size_t num_vertices, const std::string& name,
                      std::size_t line) {
    const std::string head = token.substr(0, token.find('/'));
    long long index = 0;
    std::size_t used = 0;
    try {
        index = std::stoll(head, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (used == 0 || used != head.size() || index == 0)
        obj_error(name, line, "bad face corner '" + token + "'");
    const long long n = static_cast<long long>(num_vertices);
    const long long resolved = index > 0 ? index - 1 : n + index;
    if (resolved < 0 || resolved >= n)
        obj_error(name, line,
                  "face corner '" + token + "' is out of range (" + std::to_string(num_vertices) +
                      " vertices so far)");
    return static_cast<uint32_t>(resolved);
}

}  // namespace

ObjMesh parse_obj(std::istream& input, const std::string& name) {
    ObjMesh mesh;
    mesh.groups.push_back("default");
    uint32_t group = 0u;
    std::string text;
    std::size_t line = 0;
    std::vector<uint32_t> corners;
    while (std::getline(input, text)) {
        ++line;
        std::istringstream fields(text);
        std::string kind;
        if (!(fields >> kind)) continue;
        if (kind == "o" || kind == "g") {
            std::string group_name;
            if (!(fields >> group_name)) group_name = "default";
            group = static_cast<uint32_t>(mesh.groups.size());
            for (uint32_t i = 0; i < mesh.groups.size(); ++i) {
                if (mesh.groups[i] == group_name) group = i;
            }
            if (group == mesh.groups.size()) mesh.groups.push_back(group_name);
        } else if (kind == "v") {
            double x = 0.0, y = 0.0, z = 0.0;
            if (!(fields >> x >> y >> z)) obj_error(name, line, "expected 'v x y z'");
            mesh.vertices.push_back(
                {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        } else if (kind == "f") {
            corners.clear();
            std::string token;
            while (fields >> token) {
                corners.push_back(corner_index(token, mesh.vertices.size(), name, line));
            }
            if (corners.size() < 3) obj_error(name, line, "a face needs at least 3 corners");
            for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
                mesh.faces.push_back({corners[0], corners[i], corners[i + 1]});
                mesh.face_group.push_back(group);
            }
        }
    }
    if (input.bad()) throw Error(name + ": read failed");
    return mesh;
}

ObjMesh load_obj(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw Error(path + ": cannot open");
    return parse_obj(input, path);
}

void append_mesh(SceneHost& scene, const ObjMesh& mesh, const MeshPlacement& placement) {
    if (mesh.faces.empty()) throw Error("obj: the mesh has no faces");
    if (!(placement.size > 0.0f) || !std::isfinite(placement.size))
        throw Error("obj: size must be positive and finite");
    auto check_material = [&](uint32_t id) {
        if (id >= scene.materials.size())
            throw Error("obj: material " + std::to_string(id) + " is not in the scene (" +
                        std::to_string(scene.materials.size()) + " materials)");
    };
    check_material(placement.material_id);
    std::vector<uint32_t> group_material(mesh.groups.size(), placement.material_id);
    for (const auto& [group_name, id] : placement.group_materials) {
        check_material(id);
        bool found = false;
        for (std::size_t g = 0; g < mesh.groups.size(); ++g) {
            if (mesh.groups[g] == group_name) {
                group_material[g] = id;
                found = true;
            }
        }
        if (!found) throw Error("obj: the mesh has no group '" + group_name + "'");
    }

    const double inf = std::numeric_limits<double>::infinity();
    double lo[3] = {inf, inf, inf}, hi[3] = {-inf, -inf, -inf};
    for (const vec3& v : mesh.vertices) {
        for (int a = 0; a < 3; ++a) {
            const double c = v[a];
            if (!std::isfinite(c)) throw Error("obj: a vertex is not finite");
            lo[a] = std::fmin(lo[a], c);
            hi[a] = std::fmax(hi[a], c);
        }
    }
    const double extent = std::fmax(hi[0] - lo[0], std::fmax(hi[1] - lo[1], hi[2] - lo[2]));
    if (!(extent > 0.0)) throw Error("obj: the mesh has zero extent");
    const double scale = placement.size / extent;
    const double cx = 0.5 * (lo[0] + hi[0]), cz = 0.5 * (lo[2] + hi[2]);
    const double angle = placement.rotate_y_deg * 3.14159265358979323846 / 180.0;
    const double cs = std::cos(angle), sn = std::sin(angle);

    // Scale and rotate about the center, then find the new box to set the base point.
    std::vector<double> moved(mesh.vertices.size() * 3);
    double nlo[3] = {inf, inf, inf}, nhi[3] = {-inf, -inf, -inf};
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const double x = (mesh.vertices[i].x - cx) * scale;
        const double y = (mesh.vertices[i].y - lo[1]) * scale;
        const double z = (mesh.vertices[i].z - cz) * scale;
        const double p[3] = {cs * x + sn * z, y, -sn * x + cs * z};
        for (int a = 0; a < 3; ++a) {
            moved[i * 3 + a] = p[a];
            nlo[a] = std::fmin(nlo[a], p[a]);
            nhi[a] = std::fmax(nhi[a], p[a]);
        }
    }
    const double shift[3] = {placement.base.x - 0.5 * (nlo[0] + nhi[0]), placement.base.y - nlo[1],
                             placement.base.z - 0.5 * (nlo[2] + nhi[2])};
    auto vertex = [&](uint32_t i) {
        return vec3{static_cast<float>(moved[i * 3 + 0] + shift[0]),
                    static_cast<float>(moved[i * 3 + 1] + shift[1]),
                    static_cast<float>(moved[i * 3 + 2] + shift[2])};
    };
    scene.triangles.reserve(scene.triangles.size() + mesh.faces.size());
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const auto& f = mesh.faces[i];
        scene.triangles.push_back(
            {vertex(f[0]), vertex(f[1]), vertex(f[2]), group_material[mesh.face_group[i]]});
    }
}

}  // namespace rt

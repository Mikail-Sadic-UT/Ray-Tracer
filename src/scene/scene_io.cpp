/// @file scene_io.cpp
/// @brief Strict version 2 inline JSON reader and scene validation.
#include "scene/scene_io.hpp"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

#include "camera/camera_io.hpp"
#include "scene/hash.hpp"
#include "scene/json_helpers.hpp"

namespace rt {
namespace {

using nlohmann::json;
using namespace detail;

/// Rejects counts that cannot be represented by the shared view or primitive references.
void validate_counts(size_t spheres, size_t triangles, size_t materials, size_t lights) {
    constexpr uint64_t primitive_limit = uint64_t{1} << 31;
    if (spheres >= primitive_limit || triangles >= primitive_limit ||
        spheres + triangles >= primitive_limit)
        scene_error("geometry", "total primitive count must be less than 2^31");
    if (materials > std::numeric_limits<uint32_t>::max())
        scene_error("materials", "count exceeds uint32 range");
    if (lights > std::numeric_limits<uint32_t>::max())
        scene_error("lights", "count exceeds uint32 range");
}

/// Formats an indexed path once for both parse and validation errors.
std::string indexed(const char* array, size_t index) {
    return std::string(array) + "[" + std::to_string(index) + "]";
}

}  // namespace

void validate_scene(const SceneHost& scene, bool require_light_mask) {
    validate_counts(scene.spheres.size(), scene.triangles.size(), scene.materials.size(),
                    scene.lights.size());
    validate_camera(scene.camera);
    nonnegative_vec3(scene.background, "background");
    finite_float(scene.ambient, "ambient");
    if (scene.ambient < 0.0f) scene_error("ambient", "must be nonnegative");

    for (size_t i = 0; i < scene.materials.size(); ++i) {
        const Material& material = scene.materials[i];
        const std::string path = indexed("materials", i);
        nonnegative_vec3(material.albedo, path + ".albedo");
        nonnegative_vec3(material.ks, path + ".ks");
        finite_float(material.shininess, path + ".shininess");
        if (material.shininess < 1.0f) scene_error(path + ".shininess", "must be at least 1");
        if ((material.flags & ~uint32_t{MAT_SPECULAR}) != 0)
            scene_error(path + ".flags", "unknown or reserved material flags");
    }
    for (size_t i = 0; i < scene.spheres.size(); ++i) {
        const Sphere& sphere = scene.spheres[i];
        const std::string path = indexed("spheres", i);
        finite_vec3(sphere.center, path + ".center");
        finite_float(sphere.radius, path + ".radius");
        if (sphere.radius <= 0.0f) scene_error(path + ".radius", "must be positive");
        if (sphere.material_id >= scene.materials.size())
            scene_error(path + ".material_id", "must index an existing material");
    }
    for (size_t i = 0; i < scene.triangles.size(); ++i) {
        const Triangle& triangle = scene.triangles[i];
        const std::string path = indexed("triangles", i);
        finite_vec3(triangle.v0, path + ".v0");
        finite_vec3(triangle.v1, path + ".v1");
        finite_vec3(triangle.v2, path + ".v2");
        if (triangle.material_id >= scene.materials.size())
            scene_error(path + ".material_id", "must index an existing material");
    }
    if (require_light_mask && scene.lights.size() > 32)
        scene_error("lights", "light-mask AOV requires at most 32 lights");
    for (size_t i = 0; i < scene.lights.size(); ++i) {
        const Light& light = scene.lights[i];
        const std::string path = indexed("lights", i);
        if (light.type != LIGHT_POINT)
            scene_error(path + ".type", "core supports point lights only");
        finite_vec3(light.pos_or_dir, path + ".position");
        nonnegative_vec3(light.intensity, path + ".intensity");
        if (light.pad != 0.0f) scene_error(path + ".pad", "reserved field must be zero");
    }
}

SceneHost parse_scene(const json& document) {
    const uint32_t version = read_u32(member(document, "version", ""), "version");
    if (version != 2) scene_error("version", "expected version 2");
    const json& materials = member(document, "materials", "");
    const json& lights = member(document, "lights", "");
    const json& geometry = member(document, "geometry", "");
    if (!geometry.is_object()) scene_error("geometry", "expected an object");
    if (geometry.contains("binary"))
        scene_error("geometry.binary", "binary scene loading is not implemented yet");
    const json& spheres = member(geometry, "spheres", "geometry");
    const json& triangles = member(geometry, "triangles", "geometry");
    require_array(materials, "materials");
    require_array(lights, "lights");
    require_array(spheres, "geometry.spheres");
    require_array(triangles, "geometry.triangles");
    validate_counts(spheres.size(), triangles.size(), materials.size(), lights.size());

    SceneHost scene;
    scene.camera = parse_camera(member(document, "camera", ""));
    scene.background = read_vec3(member(document, "background", ""), "background");
    scene.ambient = read_float(member(document, "ambient", ""), "ambient");
    scene.materials.reserve(materials.size());
    for (size_t i = 0; i < materials.size(); ++i) {
        const json& value = materials[i];
        const std::string path = indexed("materials", i);
        const json& specular = member(value, "specular", path);
        if (!specular.is_boolean()) scene_error(path + ".specular", "expected a boolean");
        scene.materials.push_back(
            {read_vec3(member(value, "albedo", path), path + ".albedo"),
             read_vec3(member(value, "ks", path), path + ".ks"),
             read_float(member(value, "shininess", path), path + ".shininess"),
             specular.get<bool>() ? uint32_t{MAT_SPECULAR} : 0u});
    }
    scene.lights.reserve(lights.size());
    for (size_t i = 0; i < lights.size(); ++i) {
        const json& value = lights[i];
        const std::string path = indexed("lights", i);
        const json& type = member(value, "type", path);
        if (!type.is_string() || type.get<std::string>() != "point")
            scene_error(path + ".type", "core supports point lights only");
        scene.lights.push_back(
            {LIGHT_POINT, read_vec3(member(value, "position", path), path + ".position"),
             read_vec3(member(value, "intensity", path), path + ".intensity"), 0.0f});
    }
    scene.spheres.reserve(spheres.size());
    for (size_t i = 0; i < spheres.size(); ++i) {
        const json& value = spheres[i];
        const std::string path = indexed("spheres", i);
        scene.spheres.push_back({read_vec3(member(value, "center", path), path + ".center"),
                                 read_float(member(value, "radius", path), path + ".radius"),
                                 read_u32(member(value, "material", path), path + ".material")});
    }
    scene.triangles.reserve(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        const json& value = triangles[i];
        const std::string path = indexed("triangles", i);
        const json& vertices = member(value, "v", path);
        require_array(vertices, path + ".v");
        if (vertices.size() != 3) scene_error(path + ".v", "expected exactly 3 vertices");
        scene.triangles.push_back({read_vec3(vertices[0], path + ".v[0]"),
                                   read_vec3(vertices[1], path + ".v[1]"),
                                   read_vec3(vertices[2], path + ".v[2]"),
                                   read_u32(member(value, "material", path), path + ".material")});
    }
    validate_scene(scene);
    scene.hash = scene_hash(scene.view());
    return scene;
}

SceneHost load_scene(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("scene_io: " + path + ": cannot open file");
    try {
        const json document = json::parse(input);
        if (input.bad()) throw Error("failed reading file");
        return parse_scene(document);
    } catch (const json::exception& error) {
        throw Error("scene_io: " + path + ": invalid JSON: " + error.what());
    } catch (const Error& error) {
        throw Error("scene_io: " + path + ": " + error.what());
    }
}

json scene_to_json(const SceneHost& scene) {
    validate_scene(scene);
    const auto vector = [](vec3 value) { return json::array({value.x, value.y, value.z}); };
    json document = {{"version", 2},
                     {"camera", camera_to_json(scene.camera)},
                     {"background", vector(scene.background)},
                     {"ambient", scene.ambient},
                     {"materials", json::array()},
                     {"lights", json::array()},
                     {"geometry", {{"spheres", json::array()}, {"triangles", json::array()}}}};
    for (const auto& material : scene.materials) {
        document["materials"].push_back({{"albedo", vector(material.albedo)},
                                         {"ks", vector(material.ks)},
                                         {"shininess", material.shininess},
                                         {"specular", (material.flags & MAT_SPECULAR) != 0}});
    }
    for (const auto& light : scene.lights) {
        document["lights"].push_back({{"type", "point"},
                                      {"position", vector(light.pos_or_dir)},
                                      {"intensity", vector(light.intensity)}});
    }
    for (const auto& sphere : scene.spheres) {
        document["geometry"]["spheres"].push_back({{"center", vector(sphere.center)},
                                                   {"radius", sphere.radius},
                                                   {"material", sphere.material_id}});
    }
    for (const auto& triangle : scene.triangles) {
        document["geometry"]["triangles"].push_back(
            {{"v", {vector(triangle.v0), vector(triangle.v1), vector(triangle.v2)}},
             {"material", triangle.material_id}});
    }
    return document;
}

void save_scene(const std::string& path, const SceneHost& scene) {
    // Validate and encode before opening the destination, preserving it on validation errors.
    const std::string encoded = scene_to_json(scene).dump(2) + "\n";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw Error("scene_io: " + path + ": cannot open output file");
    output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output.close();
    if (!output) throw Error("scene_io: " + path + ": failed writing file");
}

}  // namespace rt

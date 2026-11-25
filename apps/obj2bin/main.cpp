/// @file main.cpp
/// @brief obj2bin (E6): places an OBJ triangle mesh in a template scene and writes a binary
/// scene (.json + .bin) that rt_render loads like a generated one.
#include <CLI/CLI.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "scene/obj_io.hpp"
#include "scene/scene_io.hpp"

int main(int argc, char** argv) {
    try {
        CLI::App app{
            "Place an OBJ mesh in a template scene and write a binary scene for rt_render"};
        std::string obj_path, template_path, output;
        rt::MeshPlacement placement;
        std::vector<float> at;
        std::vector<std::string> group_materials;
        app.add_option("--obj", obj_path, "Wavefront OBJ triangle mesh")
            ->required()
            ->check(CLI::ExistingFile);
        app.add_option("--template", template_path,
                       "Scene JSON supplying camera, lights, materials and other geometry")
            ->required()
            ->check(CLI::ExistingFile);
        app.add_option("--out", output, "Output .json path (the .bin goes next to it)")->required();
        app.add_option("--material", placement.material_id, "Template material for the mesh");
        app.add_option("--group-material", group_materials,
                       "NAME=ID: material for one OBJ group (o/g line); repeat per group")
            ->delimiter(',');
        app.add_option("--size", placement.size, "Largest extent of the placed mesh")
            ->check(CLI::PositiveNumber);
        app.add_option("--rotate-y", placement.rotate_y_deg, "Rotation about +Y in degrees");
        app.add_option("--at", at, "Bottom center of the placed mesh (x y z)")->expected(3);
        CLI11_PARSE(app, argc, argv);
        if (at.size() == 3) placement.base = {at[0], at[1], at[2]};
        for (const std::string& item : group_materials) {
            const auto eq = item.rfind('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == item.size())
                throw std::runtime_error("--group-material expects NAME=ID, got '" + item + "'");
            placement.group_materials.emplace_back(
                item.substr(0, eq), static_cast<uint32_t>(std::stoul(item.substr(eq + 1))));
        }

        rt::SceneHost scene = rt::load_scene(template_path);
        const rt::ObjMesh mesh = rt::load_obj(obj_path);
        const std::size_t before = scene.triangles.size();
        rt::append_mesh(scene, mesh, placement);
        rt::validate_scene(scene);

        auto path = std::filesystem::path(output);
        if (path.extension() != ".json") path += ".json";
        std::string command = "obj2bin";
        for (int i = 1; i < argc; ++i) command += std::string(" ") + argv[i];
        const nlohmann::json metadata = {
            {"command", command},
            {"mesh", std::filesystem::path(obj_path).filename().string()},
            {"mesh_triangles", mesh.faces.size()},
            {"num_spheres", scene.spheres.size()},
            {"num_triangles", scene.triangles.size()}};
        rt::save_binary_scene(path.string(), scene, metadata);
        std::cerr << "obj2bin: " << mesh.faces.size() << " mesh triangles + "
                  << scene.spheres.size() << " spheres and " << before << " template triangles -> "
                  << path.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

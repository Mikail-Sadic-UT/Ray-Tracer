/// @file main.cpp
/// @brief Validates a scene and prints its counts, effective camera, and canonical hashes.
#include <CLI/CLI.hpp>
#include <exception>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "scene/hash.hpp"
#include "scene/scene_io.hpp"

int main(int argc, char** argv) {
    try {
        CLI::App app{"Inspect and validate a ray-tracer scene"};
        std::string path;
        bool verify = false;
        bool json = false;
        app.add_option("scene", path, "Version 2 scene JSON file")->required();
        app.add_flag("--verify", verify, "Print successful validation after loading");
        app.add_flag("--json", json, "Print normalized inline scene JSON");
        CLI11_PARSE(app, argc, argv);

        const auto scene = rt::load_scene(path);
        if (json) {
            std::cout << rt::scene_to_json(scene).dump(2) << '\n';
        } else {
            std::cout << "scene: " << path << '\n'
                      << "spheres: " << scene.spheres.size() << '\n'
                      << "triangles: " << scene.triangles.size() << '\n'
                      << "materials: " << scene.materials.size() << '\n'
                      << "lights: " << scene.lights.size() << '\n'
                      << "camera: " << scene.camera.width << 'x' << scene.camera.height << ", vfov "
                      << scene.camera.vfov_deg << '\n'
                      << "content_hash: 0x" << std::hex << std::setfill('0') << std::setw(16)
                      << rt::content_hash(scene.view()) << '\n'
                      << "scene_hash: 0x" << std::setw(16) << scene.hash << std::dec << '\n';
            if (verify) std::cout << "validation: PASS\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

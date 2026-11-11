/// @file main.cpp
/// @brief Repeatable uniform scene generator with benchmark height-field terrain.
#include <CLI/CLI.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

#include "scene/generator.hpp"
#include "scene/scene_io.hpp"

namespace {
/// Quotes one argument for the shell only when it needs it, so ordinary commands read as
/// typed (`rt_gen --primitives 10000 ...`) and an unusual path still round-trips.
std::string quote(const std::string& value) {
    const bool plain = !value.empty() && value.find_first_not_of(
                                             "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                                             "0123456789_-+=.,/:@%") == std::string::npos;
    if (plain) return value;
    std::string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Generate uniform benchmark scenes with height-field terrain"};
        rt::UniformSettings settings;
        std::string layout = "uniform", output;
        app.add_option("--layout", layout, "Sphere layout")->check(CLI::IsMember({"uniform"}));
        app.add_option("--primitives", settings.primitives, "Exact total primitive count")
            ->required();
        app.add_option("--seed", settings.seed, "PCG32 seed");
        app.add_option("--out", output, "Output prefix or .json path")->required();
        CLI11_PARSE(app, argc, argv);
        // Record the program as `rt_gen`, not argv[0]: the invocation path is not an input,
        // and the JSON bytes (and their manifest SHA-256) must not depend on it.
        std::string command = "rt_gen";
        for (int i = 1; i < argc; ++i) command += " " + quote(argv[i]);
        const auto scene = rt::generate_uniform(settings);
        auto path = std::filesystem::path(output);
        if (path.extension() != ".json") path += ".json";
        const nlohmann::json metadata = {{"command", command},
                                         {"layout", layout},
                                         {"seed", settings.seed},
                                         {"num_spheres", scene.spheres.size()},
                                         {"num_triangles", scene.triangles.size()}};
        rt::save_binary_scene(path.string(), scene, metadata);
        std::cerr << "Generated " << settings.primitives << " primitives at " << path.string()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

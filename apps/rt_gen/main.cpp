/// @file main.cpp
/// @brief Repeatable uniform scene generator using registered terrain stand-in S3.
#include <CLI/CLI.hpp>
#include <exception>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

#include "scene/generator.hpp"
#include "scene/scene_io.hpp"

namespace {
/// Quotes one argument for an unambiguous, reproducible generator.command string.
std::string quote(const std::string& value) {
    std::string result = "'";
    for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Generate development scenes (terrain stub S3; not a release archive)"};
        rt::UniformSettings settings;
        std::string layout = "uniform", output;
        app.add_option("--layout", layout, "Sphere layout")->check(CLI::IsMember({"uniform"}));
        app.add_option("--primitives", settings.primitives, "Exact total primitive count")
            ->required();
        app.add_option("--seed", settings.seed, "PCG32 seed");
        app.add_option("--out", output, "Output prefix or .json path")->required();
        CLI11_PARSE(app, argc, argv);
        std::string command;
        for (int i = 0; i < argc; ++i) command += (i ? " " : "") + quote(argv[i]);
        const auto scene = rt::generate_uniform(settings);
        auto path = std::filesystem::path(output);
        if (path.extension() != ".json") path += ".json";
        const nlohmann::json metadata = {{"command", command},
                                         {"layout", layout},
                                         {"seed", settings.seed},
                                         {"num_spheres", scene.spheres.size()},
                                         {"num_triangles", scene.triangles.size()},
                                         {"terrain_source", "stub-S3"}};
        rt::save_binary_scene(path.string(), scene, metadata);
        std::cerr << "Generated " << settings.primitives << " primitives at " << path.string()
                  << "; terrain stub S3: development output, not scenes-v1.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

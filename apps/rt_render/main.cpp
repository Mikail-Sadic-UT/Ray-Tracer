/// @file main.cpp
/// @brief rt_render: loads a scene, applies CLI overrides, builds our BVH if the backend needs
/// it, then prepares and renders one frame and writes the image and AOVs.
///
/// Timing modes (`--mode app|prepared`, `--runs`, `--results`) come with the harness (C9).
/// Until then this runs one preparation and one frame and prints the components to stderr.
#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "backend.hpp"
#include "bvh/bvh.hpp"
#include "core/error.hpp"
#include "cpu/cpu_backend.hpp"
#include "image/image_io.hpp"
#include "scene/scene_io.hpp"

namespace {

/// Creates the backend named by `--backend`.
/// @throws rt::Error for a backend this build doesn't provide.
std::unique_ptr<rt::Backend> make_backend(const std::string& name) {
    if (name == "cpu") return std::make_unique<rt::CpuBackend>();
    if (name == "cuda") throw rt::Error("rt_render: the cuda backend has not landed yet (C10)");
    throw rt::Error("rt_render: the " + name + " backend is not part of this build");
}

/// Parses the comma-separated `--aov` list into AovFlags.
uint32_t parse_aovs(const std::vector<std::string>& names) {
    uint32_t flags = 0;
    for (const std::string& n : names) {
        if (n == "primid") {
            flags |= rt::AOV_PRIM_ID;
        } else if (n == "lightmask") {
            flags |= rt::AOV_LIGHT_MASK;
        } else {
            throw rt::Error("rt_render: unknown AOV '" + n + "' (expected primid or lightmask)");
        }
    }
    return flags;
}

/// Converts a 3-element CLI value to vec3.
rt::vec3 to_vec3(const std::vector<float>& v) {
    return {v[0], v[1], v[2]};
}

/// Milliseconds since @p begin.
double ms_since(std::chrono::steady_clock::time_point begin) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        CLI::App app{"Render a scene with one of the ray-tracer backends"};
        std::string scene_path, backend_name = "cpu", bvh_name = "median", shade_name = "lit";
        std::string out_png, out_pfm;
        std::vector<std::string> aov_names;
        int build_threads = 1, render_threads = 1;
        uint32_t width = 0, height = 0, max_leaf_size = 4;
        std::vector<float> cam_pos, cam_look;
        float cam_vfov = 0.0f;

        app.add_option("--scene", scene_path, "Version 2 scene JSON file")->required();
        app.add_option("--backend", backend_name, "Renderer")
            ->check(CLI::IsMember({"cpu", "cuda", "embree", "optix"}))
            ->capture_default_str();
        app.add_option("--bvh", bvh_name, "Our BVH builder; none = brute force (testing only)")
            ->check(CLI::IsMember({"none", "median", "lbvh", "sah"}))
            ->capture_default_str();
        app.add_option("--max-leaf-size", max_leaf_size, "BVH leaf size limit")
            ->check(CLI::PositiveNumber)
            ->capture_default_str();
        app.add_option("--build-threads", build_threads, "Preparation threads (B)")
            ->check(CLI::PositiveNumber)
            ->capture_default_str();
        app.add_option("--render-threads", render_threads, "Render threads (R)")
            ->check(CLI::PositiveNumber)
            ->capture_default_str();
        app.add_option("--width", width, "Override the camera width")->check(CLI::PositiveNumber);
        app.add_option("--height", height, "Override the camera height")
            ->check(CLI::PositiveNumber);
        app.add_option("--cam-pos", cam_pos, "Override the camera position (x y z)")->expected(3);
        app.add_option("--cam-look", cam_look, "Override the camera target (x y z)")->expected(3);
        app.add_option("--cam-vfov", cam_vfov, "Override the vertical field of view (degrees)");
        app.add_option("--shade", shade_name, "Shading policy")
            ->check(CLI::IsMember({"lit", "normal"}))
            ->capture_default_str();
        auto* pfm_opt = app.add_option("--pfm", out_pfm, "Write linear RGB PFM");
        app.add_option("--aov", aov_names, "AOVs written next to the PFM: primid,lightmask")
            ->delimiter(',')
            ->needs(pfm_opt);
        app.add_option("--out", out_png, "Write an sRGB PNG");
        CLI11_PARSE(app, argc, argv);

        rt::Timings timings;
        auto begin = std::chrono::steady_clock::now();
        rt::SceneHost scene = rt::load_scene(scene_path);
        timings.load = ms_since(begin);

        // Effective camera and settings: JSON values, then CLI overrides, then validation.
        rt::Camera& cam = scene.camera;
        if (width != 0) cam.width = width;
        if (height != 0) cam.height = height;
        if (!cam_pos.empty()) cam.position = to_vec3(cam_pos);
        if (!cam_look.empty()) cam.look_at = to_vec3(cam_look);
        if (app.count("--cam-vfov") != 0) cam.vfov_deg = cam_vfov;
        const uint32_t aovs = parse_aovs(aov_names);
        rt::validate_scene(scene, (aovs & rt::AOV_LIGHT_MASK) != 0);
        const rt::RenderSettings settings{
            cam.width, cam.height, aovs,
            shade_name == "normal" ? rt::ShadeMode::Normal : rt::ShadeMode::Lit};

        std::unique_ptr<rt::Backend> backend = make_backend(backend_name);

        rt::Bvh bvh;
        const bool use_bvh = backend->needs_bvh() && bvh_name != "none";
        if (use_bvh) {
            rt::BvhParams params;
            params.method = bvh_name == "median" ? rt::BvhMethod::Median
                            : bvh_name == "lbvh" ? rt::BvhMethod::LBVH
                                                 : rt::BvhMethod::SAH;
            params.max_leaf_size = max_leaf_size;
            params.threads = build_threads;
            bvh = rt::build_bvh(scene.view(), params, timings);
        }

        const rt::PrepareInput input{&scene,        use_bvh ? &bvh : nullptr, settings,
                                     build_threads, render_threads,           rt::MemConfig{}};
        backend->prepare(input, timings);
        rt::Image image;
        backend->render(cam, image, timings);
        backend->release();

        // PFM and AOVs first, so an image with non-finite pixels still leaves the files that
        // diagnose it before write_png rejects it (INTERFACES §2 "Image").
        if (!out_pfm.empty()) {
            rt::write_pfm(out_pfm, image);
            rt::write_aovs(out_pfm, image);
        }
        if (!out_png.empty()) rt::write_png(out_png, image);
        if (out_pfm.empty() && out_png.empty()) {
            std::cerr << "warning: no --out or --pfm given; the image was not saved\n";
        }

        std::cerr << "rt_render: " << backend->name() << ", bvh " << bvh_name << ", " << cam.width
                  << 'x' << cam.height << ", " << shade_name << ", B=" << build_threads
                  << " R=" << render_threads << '\n';
        if (use_bvh) {
            std::cerr << "  bvh: " << bvh.stats.nodes << " nodes, " << bvh.stats.leaves
                      << " leaves, depth " << bvh.stats.depth << '\n';
        }
        std::cerr << "  load " << timings.load << " ms, bounds " << timings.bounds << " ms, build "
                  << timings.build << " ms, layout " << timings.layout << " ms, render "
                  << timings.render << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

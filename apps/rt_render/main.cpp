/// @file main.cpp
/// @brief rt_render: loads a scene, applies CLI overrides, builds our BVH if the backend needs
/// it, renders, writes the image and AOVs, and optionally writes a results file.
///
/// `--mode app` (default) runs one preparation and one frame and records T_app, from the first
/// statement of main() to the image in host memory. `--mode prepared` records T_br and T_frame
/// with the rt_bench harness: one discarded warm-up, then `--runs` runs of each
/// (BENCHMARKING "Timing quantities"). Image and results files are written after timing, so
/// `write` is in no total.
#include <CLI/CLI.hpp>
#include <array>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "backend.hpp"
#include "bench/harness.hpp"
#include "bench/machine_info.hpp"
#include "bench/results.hpp"
#include "bench/timer.hpp"
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

std::array<float, 3> to_array(rt::vec3 v) {
    return {v.x, v.y, v.z};
}

/// The `generator.command` that `rt_gen` recorded in @p path, or empty for a hand-written scene.
/// `SceneHost` doesn't keep it (CONTRACT_NOTES W11), so the JSON is read a second time.
std::string generator_command(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) return {};
    const auto gen = doc.find("generator");
    if (gen == doc.end() || !gen->is_object()) return {};
    const auto cmd = gen->find("command");
    return cmd != gen->end() && cmd->is_string() ? cmd->get<std::string>() : std::string{};
}

/// Prints one run set's summary to stderr.
void report(const char* name, const rt::RunSet& set) {
    std::cerr << "  " << name << ": median " << set.stats.median_ms << " ms (min "
              << set.stats.min_ms << ", max " << set.stats.max_ms << ", spread " << set.stats.spread
              << ", " << set.runs.size() << " runs)\n";
}

}  // namespace

int main(int argc, char** argv) {
    // T_app starts here (BENCHMARKING "Timing quantities").
    const rt::WallTimer total;
    try {
        rt::Timings startup;
        CLI::App app{"Render a scene with one of the ray-tracer backends"};
        std::string scene_path, backend_name = "cpu", bvh_name = "median", shade_name = "lit";
        std::string out_png, out_pfm, results_path, mode = "app";
        std::vector<std::string> aov_names;
        int build_threads = 1, render_threads = 1, runs = 5;
        uint32_t width = 0, height = 0, max_leaf_size = 4;
        std::vector<float> cam_pos, cam_look;
        float cam_vfov = 0.0f;
        {
            const rt::ScopedTimer args_timer(startup.args);
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
            app.add_option("--width", width, "Override the camera width")
                ->check(CLI::PositiveNumber);
            app.add_option("--height", height, "Override the camera height")
                ->check(CLI::PositiveNumber);
            app.add_option("--cam-pos", cam_pos, "Override the camera position (x y z)")
                ->expected(3);
            app.add_option("--cam-look", cam_look, "Override the camera target (x y z)")
                ->expected(3);
            app.add_option("--cam-vfov", cam_vfov, "Override the vertical field of view (degrees)");
            app.add_option("--shade", shade_name, "Shading policy")
                ->check(CLI::IsMember({"lit", "normal"}))
                ->capture_default_str();
            app.add_option("--mode", mode,
                           "app: one cold pass, records T_app; prepared: records T_br and T_frame")
                ->check(CLI::IsMember({"app", "prepared"}))
                ->capture_default_str();
            app.add_option("--runs", runs, "Measured runs of T_br and T_frame (--mode prepared)")
                ->check(CLI::PositiveNumber)
                ->capture_default_str();
            auto* pfm_opt = app.add_option("--pfm", out_pfm, "Write linear RGB PFM");
            app.add_option("--aov", aov_names, "AOVs written next to the PFM: primid,lightmask")
                ->delimiter(',')
                ->needs(pfm_opt);
            app.add_option("--out", out_png, "Write an sRGB PNG");
            app.add_option("--results", results_path, "Write a results file (INTERFACES §8)");
            CLI11_PARSE(app, argc, argv);
            if (mode == "app" && app.count("--runs") != 0) {
                throw rt::Error("rt_render: --runs applies only to --mode prepared");
            }
        }

        rt::SceneHost scene;
        {
            const rt::ScopedTimer load_timer(startup.load);
            scene = rt::load_scene(scene_path);
        }

        // Effective camera and settings: JSON values, then CLI overrides, then validation.
        rt::Camera& cam = scene.camera;
        std::vector<std::string> overridden;
        if (width != 0) {
            cam.width = width;
            overridden.push_back("width");
        }
        if (height != 0) {
            cam.height = height;
            overridden.push_back("height");
        }
        if (!cam_pos.empty()) {
            cam.position = to_vec3(cam_pos);
            overridden.push_back("position");
        }
        if (!cam_look.empty()) {
            cam.look_at = to_vec3(cam_look);
            overridden.push_back("look_at");
        }
        if (app.count("--cam-vfov") != 0) {
            cam.vfov_deg = cam_vfov;
            overridden.push_back("vfov_deg");
        }
        const uint32_t aovs = parse_aovs(aov_names);
        rt::validate_scene(scene, (aovs & rt::AOV_LIGHT_MASK) != 0);
        const rt::RenderSettings settings{
            cam.width, cam.height, aovs,
            shade_name == "normal" ? rt::ShadeMode::Normal : rt::ShadeMode::Lit};

        std::unique_ptr<rt::Backend> backend = make_backend(backend_name);
        const bool use_bvh = backend->needs_bvh() && bvh_name != "none";
        rt::BvhParams params;
        params.method = bvh_name == "lbvh"  ? rt::BvhMethod::LBVH
                        : bvh_name == "sah" ? rt::BvhMethod::SAH
                                            : rt::BvhMethod::Median;
        params.max_leaf_size = max_leaf_size;
        params.threads = build_threads;

        rt::Bvh bvh;
        rt::Image image;
        // One preparation and one frame: build the BVH, prepare, render. T_br starts here.
        const auto build_and_render = [&](rt::Timings& t) {
            if (use_bvh) bvh = rt::build_bvh(scene.view(), params, t);
            const rt::PrepareInput input{&scene,        use_bvh ? &bvh : nullptr, settings,
                                         build_threads, render_threads,           rt::MemConfig{}};
            backend->prepare(input, t);
            backend->render(cam, image, t);
        };

        rt::ResultsDoc doc;
        if (mode == "app") {
            rt::Timings t = startup;
            build_and_render(t);
            // T_app ends with the image in host memory, before any file is written.
            doc.app = rt::make_app_record(total.elapsed_ms(), t);
        } else {
            rt::HarnessOptions options;
            options.runs = runs;
            // The reset is untimed: release() and dropping the BVH would otherwise land in
            // T_br as unattributed time, because prepare() releases first.
            doc.build_and_render = rt::measure_build_and_render(
                options,
                [&] {
                    backend->release();
                    bvh = rt::Bvh{};
                },
                build_and_render);
            // The last T_br run left the backend prepared; T_frame reuses that preparation.
            doc.frame =
                rt::measure_frame(options, [&](rt::Timings& t) { backend->render(cam, image, t); });
        }
        // Components to print: the T_app run, or the T_br run whose total is the median.
        const rt::Timings& shown =
            doc.app ? doc.app->components
                    : doc.build_and_render->runs[doc.build_and_render->stats.median_run_index]
                          .components;

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

        if (!results_path.empty()) {
            doc.machine = rt::collect_machine_info();
            doc.build = rt::collect_build_info();
            doc.scene.json = scene_path;
            doc.scene.hash = scene.hash;
            doc.scene.generator_command = generator_command(scene_path);
            doc.scene.num_spheres = static_cast<uint32_t>(scene.spheres.size());
            doc.scene.num_triangles = static_cast<uint32_t>(scene.triangles.size());
            doc.scene.num_primitives = doc.scene.num_spheres + doc.scene.num_triangles;
            doc.scene.num_lights = static_cast<uint32_t>(scene.lights.size());
            rt::ConfigInfo& c = doc.config;
            c.backend = backend->name();
            c.pipeline = "none";  // only the CUDA backends have mega / wavefront (C10, E5)
            c.bvh = bvh_name;
            c.max_leaf_size = max_leaf_size;
            c.build_threads = build_threads;
            c.render_threads = render_threads;
            c.width = cam.width;
            c.height = cam.height;
            if (aovs & rt::AOV_PRIM_ID) c.aovs.push_back("primid");
            if (aovs & rt::AOV_LIGHT_MASK) c.aovs.push_back("lightmask");
            c.camera = {to_array(cam.position), to_array(cam.look_at), to_array(cam.up),
                        cam.vfov_deg, overridden};
            c.query = use_bvh ? "bvh" : "brute";
            c.shade = shade_name;
            if (use_bvh) {
                doc.bvh_stats = rt::BvhStatsInfo{bvh.stats.nodes, bvh.stats.leaves, bvh.stats.depth,
                                                 bvh.stats.fallbacks, bvh.stats.sah_cost};
            }
            doc.image.path = !out_png.empty() ? out_png : out_pfm;
            doc.image.rgb_hash = rt::hash_rgb(image.rgb.data(), image.rgb.size());
            rt::write_results(results_path, doc);
        }
        backend->release();

        std::cerr << "rt_render: " << backend->name() << ", bvh " << bvh_name << ", " << cam.width
                  << 'x' << cam.height << ", " << shade_name << ", B=" << build_threads
                  << " R=" << render_threads << ", mode " << mode << '\n';
        if (use_bvh) {
            std::cerr << "  bvh: " << bvh.stats.nodes << " nodes, " << bvh.stats.leaves
                      << " leaves, depth " << bvh.stats.depth << '\n';
        }
        if (doc.app) {
            std::cerr << "  T_app " << doc.app->total_ms << " ms (unattributed "
                      << doc.app->unattributed_ms << " ms)\n";
        } else {
            report("T_br", *doc.build_and_render);
            report("T_frame", *doc.frame);
        }
        std::cerr << "  args " << startup.args << " ms, load " << startup.load << " ms, bounds "
                  << shown.bounds << " ms, build " << shown.build << " ms, layout " << shown.layout
                  << " ms, render " << shown.render << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}

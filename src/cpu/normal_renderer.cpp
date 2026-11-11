/// @file normal_renderer.cpp
/// @brief Deterministic tiled CPU rendering through the shared pixel implementation.
#include "cpu/normal_renderer.hpp"

#include <chrono>

#include "core/error.hpp"
#include "cpu/tile_loop.hpp"
#include "scene/scene_io.hpp"
#include "trace/brute.hpp"

namespace rt {

void render_normal(const SceneHost& scene, Image& output, const NormalRenderSettings& settings,
                   Timings& timings) {
    if (settings.threads < 1) throw Error("cpu: render thread count must be positive");
    validate_scene(scene, settings.light_masks);
    cpu::resize_image(output, scene.camera, settings.prim_ids, settings.light_masks);
    const Scene view = scene.view();
    const BruteQuery query{view};
    const auto begin = std::chrono::steady_clock::now();
    cpu::render_tiles(scene.camera, view, query, NormalShading{}, output, settings.threads);
    timings.render =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

}  // namespace rt

/// @file normal_renderer.cpp
/// @brief Deterministic tiled CPU rendering through the shared pixel implementation.
#include "cpu/normal_renderer.hpp"

#include <algorithm>
#include <chrono>

#include "core/error.hpp"
#include "scene/scene_io.hpp"
#include "shade/render_pixel.hpp"
#include "trace/brute.hpp"

namespace rt {

void render_normal(const SceneHost& scene, Image& output, const NormalRenderSettings& settings,
                   Timings& timings) {
    if (settings.threads < 1) throw Error("cpu: render thread count must be positive");
    validate_scene(scene, settings.light_masks);
    const Camera& camera = scene.camera;
    const size_t pixels = static_cast<size_t>(camera.width) * camera.height;
    output.width = camera.width;
    output.height = camera.height;
    output.rgb.resize(pixels * 3);
    output.prim_id.resize(settings.prim_ids ? pixels : 0);
    output.light_mask.resize(settings.light_masks ? pixels : 0);
    const Scene view = scene.view();
    const BruteQuery query{view};
    const uint32_t tiles_x = (camera.width + 15) / 16;
    const uint32_t tiles_y = (camera.height + 15) / 16;
    const auto begin = std::chrono::steady_clock::now();
#pragma omp parallel for schedule(dynamic) num_threads(settings.threads)
    for (int tile = 0; tile < static_cast<int>(tiles_x * tiles_y); ++tile) {
        const uint32_t x0 = (static_cast<uint32_t>(tile) % tiles_x) * 16;
        const uint32_t y0 = (static_cast<uint32_t>(tile) / tiles_x) * 16;
        for (uint32_t y = y0; y < std::min(y0 + 16, camera.height); ++y) {
            for (uint32_t x = x0; x < std::min(x0 + 16, camera.width); ++x) {
                const PixelResult pixel =
                    render_pixel<BruteQuery, NormalShading>(camera, x, y, view, query);
                const size_t index = static_cast<size_t>(y) * camera.width + x;
                output.rgb[3 * index] = pixel.rgb.x;
                output.rgb[3 * index + 1] = pixel.rgb.y;
                output.rgb[3 * index + 2] = pixel.rgb.z;
                if (settings.prim_ids) output.prim_id[index] = pixel.prim_id;
                if (settings.light_masks) output.light_mask[index] = pixel.light_mask;
            }
        }
    }
    timings.render =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}

}  // namespace rt

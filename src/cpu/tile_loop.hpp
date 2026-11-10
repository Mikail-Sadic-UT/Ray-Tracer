/// @file tile_loop.hpp
/// @brief Deterministic tiled CPU pixel loop shared by render_normal and CpuBackend.
///
/// Host-only, internal to rt_cpu.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "camera/camera.hpp"
#include "image/image.hpp"
#include "scene/scene.hpp"
#include "shade/render_pixel.hpp"

namespace rt {
namespace cpu {

/// Tile edge in pixels (ROADMAP C7: 16x16 tiles, dynamic schedule).
constexpr uint32_t kTileSize = 16;

/// Resizes @p out to the camera's resolution: RGB always, each AOV only when requested (an
/// unrequested AOV is cleared).
inline void resize_image(Image& out, const Camera& cam, bool prim_ids, bool light_masks) {
    const size_t pixels = static_cast<size_t>(cam.width) * cam.height;
    out.width = cam.width;
    out.height = cam.height;
    out.rgb.resize(pixels * 3);
    out.prim_id.resize(prim_ids ? pixels : 0);
    out.light_mask.resize(light_masks ? pixels : 0);
}

/// Renders every pixel with `render_pixel<Query, Shading>` over dynamic 16x16 OpenMP tiles.
///
/// Each pixel depends only on its own coordinates and writes only its own slots, so the output
/// is bit-identical for every thread count.
/// @pre @p out was sized by resize_image() for @p cam with the same AOV choices.
/// @pre `threads >= 1`.
template <class Query, class Shading>
void render_tiles(const Camera& cam, const Scene& s, const Query& q, Shading shade, Image& out,
                  int threads) {
    const bool prim_ids = !out.prim_id.empty();
    const bool light_masks = !out.light_mask.empty();
    const uint32_t tiles_x = (cam.width + kTileSize - 1) / kTileSize;
    const uint32_t tiles_y = (cam.height + kTileSize - 1) / kTileSize;
    const int tiles = static_cast<int>(tiles_x * tiles_y);
#pragma omp parallel for schedule(dynamic) num_threads(threads)
    for (int tile = 0; tile < tiles; ++tile) {
        const uint32_t x0 = (static_cast<uint32_t>(tile) % tiles_x) * kTileSize;
        const uint32_t y0 = (static_cast<uint32_t>(tile) / tiles_x) * kTileSize;
        const uint32_t x1 = std::min(x0 + kTileSize, cam.width);
        const uint32_t y1 = std::min(y0 + kTileSize, cam.height);
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                const PixelResult p = render_pixel<Query, Shading>(cam, x, y, s, q, shade);
                const size_t i = static_cast<size_t>(y) * cam.width + x;
                out.rgb[3 * i] = p.rgb.x;
                out.rgb[3 * i + 1] = p.rgb.y;
                out.rgb[3 * i + 2] = p.rgb.z;
                if (prim_ids) out.prim_id[i] = p.prim_id;
                if (light_masks) out.light_mask[i] = p.light_mask;
            }
        }
    }
}

}  // namespace cpu
}  // namespace rt

/// @file compare.cpp
/// @brief Symmetric hit explanations, shadow explanations, budgets and linear color checks.
#include "image/compare.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "image/image_io.hpp"
#include "image/io_helpers.hpp"

namespace rt {
namespace {

/// Checks the neighbor IDs/masks without allowing rows to wrap at image edges.
bool neighbor_has(const std::vector<uint32_t>& values, uint32_t width, uint32_t height, uint32_t x,
                  uint32_t y, uint32_t value) {
    for (int dy = -1; dy <= 1; ++dy) {
        const int yy = static_cast<int>(y) + dy;
        if (yy < 0 || yy >= static_cast<int>(height)) continue;
        for (int dx = -1; dx <= 1; ++dx) {
            const int xx = static_cast<int>(x) + dx;
            if (xx >= 0 && xx < static_cast<int>(width) &&
                values[static_cast<size_t>(yy) * width + static_cast<size_t>(xx)] == value)
                return true;
        }
    }
    return false;
}

/// Matches PNG conversion for the informational 2/255 sRGB summary.
int srgb8(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    const float encoded =
        value <= 0.0031308f ? 12.92f * value : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<int>(std::lround(encoded * 255.0f));
}

void validate_image(const Image& image, const char* name) {
    const size_t count = detail::image_pixels(image.width, image.height, name);
    if (image.rgb.size() != 3 * count || image.prim_id.size() != count ||
        image.light_mask.size() != count)
        detail::image_error(name,
                            "comparison requires RGB, prim_id and light_mask for every pixel");
}

}  // namespace

Image read_comparison_image(const std::string& path) {
    Image image = read_pfm(path);
    auto ids_path = std::filesystem::path(path);
    auto mask_path = ids_path;
    ids_path.replace_extension(".primid.u32img");
    mask_path.replace_extension(".lightmask.u32img");
    auto ids = read_u32img(ids_path.string());
    auto masks = read_u32img(mask_path.string());
    if (ids.width != image.width || ids.height != image.height || masks.width != image.width ||
        masks.height != image.height)
        detail::image_error(path, "PFM and AOV dimensions differ");
    image.prim_id = std::move(ids.pixels);
    image.light_mask = std::move(masks.pixels);
    return image;
}

CompareResult compare_images(const Image& reference, const Image& test,
                             const CompareSettings& settings) {
    validate_image(reference, "reference");
    validate_image(test, "test");
    if (reference.width != test.width || reference.height != test.height)
        detail::image_error("comparison", "reference and test dimensions differ");
    if (!std::isfinite(settings.absolute) || settings.absolute < 0 ||
        !std::isfinite(settings.relative) || settings.relative < 0 ||
        !std::isfinite(settings.mismatch_fraction) || settings.mismatch_fraction < 0 ||
        settings.mismatch_fraction > 1)
        detail::image_error("comparison", "invalid tolerance or mismatch budget");
    CompareResult result;
    result.pixels = reference.prim_id.size();
    for (uint32_t y = 0; y < reference.height; ++y) {
        for (uint32_t x = 0; x < reference.width; ++x) {
            const size_t i = static_cast<size_t>(y) * reference.width + x;
            bool finite = true, close = true, shading_ok = true;
            for (size_t channel = 0; channel < 3; ++channel) {
                const float a = reference.rgb[i * 3 + channel], b = test.rgb[i * 3 + channel];
                if (!std::isfinite(a) || !std::isfinite(b)) {
                    finite = false;
                    close = false;
                    continue;
                }
                close = close && std::abs(srgb8(a) - srgb8(b)) <= 2;
                shading_ok =
                    shading_ok &&
                    std::abs(static_cast<double>(a) - b) <=
                        settings.absolute + settings.relative * std::abs(static_cast<double>(a));
            }
            result.nonfinite += !finite;
            result.srgb_close += close;
            if (reference.prim_id[i] != test.prim_id[i]) {
                const bool explained =
                    neighbor_has(reference.prim_id, reference.width, reference.height, x, y,
                                 test.prim_id[i]) &&
                    neighbor_has(test.prim_id, test.width, test.height, x, y, reference.prim_id[i]);
                result.unexplained_hits += !explained;
                result.explained_mismatches += explained;
            } else if (reference.light_mask[i] != test.light_mask[i]) {
                const bool explained = neighbor_has(reference.light_mask, reference.width,
                                                    reference.height, x, y, test.light_mask[i]);
                result.unexplained_shadows += !explained;
                result.explained_mismatches += explained;
            } else if (finite && !shading_ok) {
                ++result.shading_mismatches;
            }
        }
    }
    result.budget_pass = static_cast<double>(result.explained_mismatches) <=
                         settings.mismatch_fraction * static_cast<double>(result.pixels);
    return result;
}

}  // namespace rt

/// @file write_png.cpp
/// @brief Linear RGB to sRGB PNG conversion using the pinned stb_image_write dependency.
#include <algorithm>
#include <cmath>

#include "image/image_io.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "image/io_helpers.hpp"

namespace rt {

void write_png(const std::string& path, const Image& image) {
    const size_t pixels = detail::image_pixels(image.width, image.height, path);
    if (image.rgb.size() != pixels * 3)
        detail::image_error(path, "RGB size does not match dimensions");
    std::vector<unsigned char> encoded(image.rgb.size());
    for (size_t i = 0; i < image.rgb.size(); ++i) {
        if (!std::isfinite(image.rgb[i]))
            detail::image_error(path, "nonfinite RGB sample at index " + std::to_string(i));
        const float linear = std::clamp(image.rgb[i], 0.0f, 1.0f);
        const float srgb = linear <= 0.0031308f ? 12.92f * linear
                                                : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        encoded[i] = static_cast<unsigned char>(std::lround(srgb * 255.0f));
    }
    // stb's default orientation writes the first input row at the top.
    if (!stbi_write_png(path.c_str(), static_cast<int>(image.width), static_cast<int>(image.height),
                        3, encoded.data(), static_cast<int>(image.width * 3)))
        detail::image_error(path, "failed writing PNG");
}

}  // namespace rt

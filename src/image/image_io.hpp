/// @file image_io.hpp
/// @brief Host image IO for PNG, linear RGB PFM, and lossless single-channel uint32 AOVs.
#pragma once

#include <string>

#include "image/image.hpp"

namespace rt {

/// Single-channel integer image read from a version 1 .u32img file.
struct U32Image {
    uint32_t width = 0, height = 0;  ///< Image dimensions.
    std::vector<uint32_t> pixels;    ///< Row-major values, row zero at the top.
};

/// Clamps finite linear RGB to [0,1], sRGB encodes, and writes an 8-bit RGB PNG.
/// @throws Error on invalid dimensions, RGB storage, nonfinite samples, or write failure.
void write_png(const std::string& path, const Image& image);

/// Writes RGB PFM, little-endian with scale -1 and rows stored bottom to top.
/// Preserves unclamped float bits, including nonfinite values for comparison diagnostics.
/// @throws Error on invalid dimensions, RGB storage, or file errors.
void write_pfm(const std::string& path, const Image& image);

/// Reads color PFM in either byte order; applies the absolute scale and returns top-first RGB.
/// Nonfinite pixel values remain available to rt_compare's nonfinite check.
/// @throws Error on malformed headers, unsupported dimensions, or incorrect payload size.
Image read_pfm(const std::string& path);

/// Writes version 1 RTU32IMG, explicitly little-endian with zero reserved fields.
/// @throws Error on invalid dimensions, data size, or file errors.
void write_u32img(const std::string& path, uint32_t width, uint32_t height,
                  const std::vector<uint32_t>& pixels);

/// Reads and validates version, channels, reserved fields, dimensions, and exact file size.
/// @throws Error on a malformed file or read failure.
U32Image read_u32img(const std::string& path);

/// Writes requested nonempty AOV arrays next to a PFM: stem.primid.u32img and
/// stem.lightmask.u32img. Does not write RGB or retrace any rays.
/// @throws Error on invalid dimensions, AOV sizes, or file errors.
void write_aovs(const std::string& pfm_path, const Image& image);

}  // namespace rt

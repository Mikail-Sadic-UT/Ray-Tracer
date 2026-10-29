/// @file image_io.cpp
/// @brief Portable PFM and version 1 .u32img file IO.
#include "image/image_io.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <sstream>

#include "image/io_helpers.hpp"

namespace rt {
namespace {

using namespace detail;

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "PFM requires IEEE-754 32-bit floats");

/// Converts a float to its unchanged IEEE bit pattern.
uint32_t float_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// Converts an IEEE bit pattern to a float without arithmetic or clamping.
float bits_float(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/// Requires no extra header tokens after a parsed value.
void require_line_end(std::istringstream& line, const std::string& path) {
    line >> std::ws;
    if (!line.eof()) image_error(path, "unexpected PFM header tokens");
}

/// Reads one PFM header line, accepting Unix and Windows line endings.
std::string header_line(std::istream& input, const std::string& path) {
    std::string line;
    if (!std::getline(input, line)) image_error(path, "truncated PFM header");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

}  // namespace

void write_pfm(const std::string& path, const Image& image) {
    const size_t pixels = image_pixels(image.width, image.height, path);
    if (image.rgb.size() != pixels * 3) image_error(path, "RGB size does not match dimensions");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) image_error(path, "cannot open output file");
    output.imbue(std::locale::classic());
    output << "PF\n" << image.width << ' ' << image.height << "\n-1.0\n";
    const size_t samples = static_cast<size_t>(image.width) * 3;
    std::vector<unsigned char> row(samples * 4);
    for (uint32_t y = image.height; y-- > 0;) {
        for (size_t x = 0; x < samples; ++x)
            encode_u32(row.data() + x * 4, float_bits(image.rgb[y * samples + x]));
        write_bytes(output, row.data(), row.size(), path);
    }
    output.close();
    if (!output) image_error(path, "failed closing output file");
}

Image read_pfm(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) image_error(path, "cannot open input file");
    if (header_line(input, path) != "PF") image_error(path, "expected RGB PFM magic PF");
    std::istringstream dimensions(header_line(input, path));
    dimensions.imbue(std::locale::classic());
    uint64_t width = 0, height = 0;
    if (!(dimensions >> width >> height) || width < 1 || height < 1 || width > 16384 ||
        height > 16384)
        image_error(path, "invalid PFM dimensions");
    require_line_end(dimensions, path);
    std::istringstream scale_line(header_line(input, path));
    scale_line.imbue(std::locale::classic());
    double scale = 0;
    if (!(scale_line >> scale) || !std::isfinite(scale) || scale == 0)
        image_error(path, "invalid PFM scale");
    require_line_end(scale_line, path);
    const size_t samples = static_cast<size_t>(width) * 3;
    require_payload(input, samples * height * 4, path);

    Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgb.resize(samples * image.height);
    std::vector<unsigned char> row(samples * 4);
    for (uint32_t y = image.height; y-- > 0;) {
        read_bytes(input, row.data(), row.size(), path);
        for (size_t x = 0; x < samples; ++x) {
            const float value = bits_float(decode_u32(row.data() + x * 4, scale < 0));
            image.rgb[y * samples + x] =
                std::abs(scale) == 1.0 ? value : static_cast<float>(value * std::abs(scale));
        }
    }
    return image;
}

void write_u32img(const std::string& path, uint32_t width, uint32_t height,
                  const std::vector<uint32_t>& pixels) {
    if (pixels.size() != image_pixels(width, height, path))
        image_error(path, "integer data size does not match dimensions");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) image_error(path, "cannot open output file");
    unsigned char header[32]{};
    std::memcpy(header, "RTU32IMG", 8);
    encode_u32(header + 8, 1);
    encode_u32(header + 12, width);
    encode_u32(header + 16, height);
    encode_u32(header + 20, 1);
    write_bytes(output, header, sizeof(header), path);
    std::vector<unsigned char> row(static_cast<size_t>(width) * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x)
            encode_u32(row.data() + x * 4, pixels[static_cast<size_t>(y) * width + x]);
        write_bytes(output, row.data(), row.size(), path);
    }
    output.close();
    if (!output) image_error(path, "failed closing output file");
}

U32Image read_u32img(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) image_error(path, "cannot open input file");
    unsigned char header[32];
    read_bytes(input, header, sizeof(header), path);
    if (std::memcmp(header, "RTU32IMG", 8) != 0) image_error(path, "invalid RTU32IMG magic");
    if (decode_u32(header + 8) != 1) image_error(path, "unsupported RTU32IMG version");
    if (decode_u32(header + 20) != 1) image_error(path, "RTU32IMG channels must be 1");
    for (unsigned i = 24; i < 32; ++i)
        if (header[i] != 0) image_error(path, "RTU32IMG reserved fields must be zero");
    U32Image image;
    image.width = decode_u32(header + 12);
    image.height = decode_u32(header + 16);
    const size_t count = image_pixels(image.width, image.height, path);
    require_payload(input, count * 4, path);
    image.pixels.resize(count);
    std::vector<unsigned char> row(static_cast<size_t>(image.width) * 4);
    for (uint32_t y = 0; y < image.height; ++y) {
        read_bytes(input, row.data(), row.size(), path);
        for (uint32_t x = 0; x < image.width; ++x)
            image.pixels[static_cast<size_t>(y) * image.width + x] = decode_u32(row.data() + x * 4);
    }
    return image;
}

void write_aovs(const std::string& pfm_path, const Image& image) {
    const size_t count = image_pixels(image.width, image.height, pfm_path);
    if ((!image.prim_id.empty() && image.prim_id.size() != count) ||
        (!image.light_mask.empty() && image.light_mask.size() != count))
        image_error(pfm_path, "AOV size does not match dimensions");
    const auto sidecar = [&](const char* extension) {
        auto path = std::filesystem::path(pfm_path);
        return path.replace_extension(extension).string();
    };
    if (!image.prim_id.empty())
        write_u32img(sidecar(".primid.u32img"), image.width, image.height, image.prim_id);
    if (!image.light_mask.empty())
        write_u32img(sidecar(".lightmask.u32img"), image.width, image.height, image.light_mask);
}

}  // namespace rt

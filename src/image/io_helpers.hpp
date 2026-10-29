/// @file io_helpers.hpp
/// @brief Internal image shape, error, and byte-encoding helpers. Host-only.
#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

#include "core/error.hpp"

namespace rt::detail {

/// Reports an image error with its source or destination path.
[[noreturn]] inline void image_error(const std::string& path, const std::string& reason) {
    throw Error("image_io: " + path + ": " + reason);
}

/// Validates dimensions before multiplying or allocating storage.
inline size_t image_pixels(uint32_t width, uint32_t height, const std::string& path) {
    if (width < 1 || height < 1 || width > 16384 || height > 16384)
        image_error(path, "width and height must be in [1, 16384]");
    return static_cast<size_t>(width) * height;
}

/// Encodes one uint32 in little-endian byte order, independent of host byte order.
inline void encode_u32(unsigned char* bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<unsigned char>(value >> (8 * i));
}

/// Decodes one uint32 from either byte order.
inline uint32_t decode_u32(const unsigned char* bytes, bool little = true) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<uint32_t>(bytes[little ? i : 3 - i]) << (8 * i);
    return value;
}

/// Checks payload size before allocating based on dimensions from an untrusted header.
inline void require_payload(std::istream& input, uint64_t bytes, const std::string& path) {
    const auto begin = input.tellg();
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (begin < 0 || end < begin || static_cast<uint64_t>(end - begin) != bytes)
        image_error(path, "incorrect payload size (truncated or trailing data)");
    input.seekg(begin);
    if (!input) image_error(path, "cannot seek input file");
}

/// Reads exactly the requested bytes.
inline void read_bytes(std::istream& input, unsigned char* bytes, size_t count,
                       const std::string& path) {
    input.read(reinterpret_cast<char*>(bytes), static_cast<std::streamsize>(count));
    if (!input) image_error(path, "truncated file or read failure");
}

/// Writes exactly the requested bytes.
inline void write_bytes(std::ostream& output, const unsigned char* bytes, size_t count,
                        const std::string& path) {
    output.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(count));
    if (!output) image_error(path, "write failure");
}

}  // namespace rt::detail

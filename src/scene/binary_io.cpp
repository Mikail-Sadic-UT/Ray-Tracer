/// @file binary_io.cpp
/// @brief Explicit byte encoding for RTSCENE version 2; never serializes native struct bytes.
#include "scene/binary_io.hpp"

#include <cstring>
#include <fstream>
#include <limits>

#include "core/error.hpp"
#include "scene/hash.hpp"
#include "scene/scene_io.hpp"

namespace rt {
namespace {

/// Reports a binary IO error with its file path.
[[noreturn]] void fail(const std::string& path, const char* message) {
    throw Error("scene_io: " + path + ": " + message);
}

/// Writes an integer as n little-endian bytes.
void put(unsigned char* bytes, uint64_t value, unsigned n) {
    for (unsigned i = 0; i < n; ++i) bytes[i] = static_cast<unsigned char>(value >> (8 * i));
}

/// Reads an integer from n little-endian bytes.
uint64_t get(const unsigned char* bytes, unsigned n) {
    uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i) value |= uint64_t{bytes[i]} << (8 * i);
    return value;
}

/// Encodes one float's exact IEEE representation.
void put_float(unsigned char* bytes, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    put(bytes, bits, 4);
}

/// Decodes one float's exact IEEE representation.
float get_float(const unsigned char* bytes) {
    const uint32_t bits = static_cast<uint32_t>(get(bytes, 4));
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void put_vector(unsigned char* bytes, vec3 value) {
    put_float(bytes, value.x);
    put_float(bytes + 4, value.y);
    put_float(bytes + 8, value.z);
}

vec3 get_vector(const unsigned char* bytes) {
    return {get_float(bytes), get_float(bytes + 4), get_float(bytes + 8)};
}

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "RTSCENE requires IEEE-754 32-bit floats");

}  // namespace

void write_geometry(const std::string& path, const SceneHost& scene) {
    validate_scene(scene);
    const uint64_t sphere_end = 64 + uint64_t{scene.spheres.size()} * 20;
    const uint64_t triangles_offset = (sphere_end + 15) & ~uint64_t{15};
    unsigned char header[64]{};
    std::memcpy(header, "RTSCENE\0", 8);
    put(header + 8, 2, 4);
    put(header + 12, 64, 4);
    put(header + 16, scene.spheres.size(), 4);
    put(header + 20, scene.triangles.size(), 4);
    put(header + 24, 64, 8);
    put(header + 32, triangles_offset, 8);
    put(header + 40, content_hash(scene.view()), 8);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) fail(path, "cannot open binary output");
    output.write(reinterpret_cast<const char*>(header), 64);
    unsigned char record[40]{};
    for (const auto& sphere : scene.spheres) {
        put_vector(record, sphere.center);
        put_float(record + 12, sphere.radius);
        put(record + 16, sphere.material_id, 4);
        output.write(reinterpret_cast<const char*>(record), 20);
    }
    const unsigned char zeros[16]{};
    output.write(reinterpret_cast<const char*>(zeros),
                 static_cast<std::streamsize>(triangles_offset - sphere_end));
    for (const auto& triangle : scene.triangles) {
        put_vector(record, triangle.v0);
        put_vector(record + 12, triangle.v1);
        put_vector(record + 24, triangle.v2);
        put(record + 36, triangle.material_id, 4);
        output.write(reinterpret_cast<const char*>(record), 40);
    }
    output.close();
    if (!output) fail(path, "failed writing binary geometry");
}

GeometryData read_geometry(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) fail(path, "cannot open binary geometry");
    unsigned char header[64];
    input.read(reinterpret_cast<char*>(header), 64);
    if (!input) fail(path, "truncated binary header");
    if (std::memcmp(header, "RTSCENE\0", 8) != 0) fail(path, "wrong RTSCENE magic");
    if (get(header + 8, 4) != 2) fail(path, "unsupported binary version");
    if (get(header + 12, 4) != 64) fail(path, "unsupported binary header size");
    for (unsigned i = 48; i < 64; ++i)
        if (header[i] != 0) fail(path, "binary reserved fields must be zero");
    const uint64_t spheres = get(header + 16, 4), triangles = get(header + 20, 4);
    if (spheres + triangles >= (uint64_t{1} << 31))
        fail(path, "total primitive count must be less than 2^31");
    const uint64_t sphere_offset = get(header + 24, 8);
    const uint64_t triangle_offset = get(header + 32, 8);
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) fail(path, "cannot determine binary file size");
    const uint64_t size = static_cast<uint64_t>(end);
    // Subtractions after bounds checks avoid integer overflow on malicious offsets.
    if (sphere_offset < 64 || sphere_offset % 16 || triangle_offset % 16 || sphere_offset > size ||
        triangle_offset > size || triangle_offset < sphere_offset ||
        spheres * 20 > triangle_offset - sphere_offset || triangles * 40 != size - triangle_offset)
        fail(path, "invalid binary offsets or payload size");
    GeometryData geometry;
    geometry.spheres.resize(static_cast<size_t>(spheres));
    geometry.triangles.resize(static_cast<size_t>(triangles));
    unsigned char record[40];
    input.seekg(static_cast<std::streamoff>(sphere_offset));
    for (auto& sphere : geometry.spheres) {
        input.read(reinterpret_cast<char*>(record), 20);
        if (!input) fail(path, "truncated sphere array");
        sphere = {get_vector(record), get_float(record + 12),
                  static_cast<uint32_t>(get(record + 16, 4))};
    }
    input.seekg(static_cast<std::streamoff>(triangle_offset));
    for (auto& triangle : geometry.triangles) {
        input.read(reinterpret_cast<char*>(record), 40);
        if (!input) fail(path, "truncated triangle array");
        triangle = {get_vector(record), get_vector(record + 12), get_vector(record + 24),
                    static_cast<uint32_t>(get(record + 36, 4))};
    }
    Scene view{};
    view.spheres = geometry.spheres.empty() ? nullptr : geometry.spheres.data();
    view.num_spheres = static_cast<uint32_t>(spheres);
    view.triangles = geometry.triangles.empty() ? nullptr : geometry.triangles.data();
    view.num_triangles = static_cast<uint32_t>(triangles);
    geometry.hash = content_hash(view);
    if (geometry.hash != get(header + 40, 8)) fail(path, "binary content hash mismatch");
    return geometry;
}

}  // namespace rt

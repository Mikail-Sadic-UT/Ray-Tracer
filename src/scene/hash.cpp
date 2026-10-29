/// @file hash.cpp
/// @brief Field-by-field little-endian FNV-1a encoding from INTERFACES section 2.
#include "scene/hash.hpp"

#include <cstring>
#include <limits>

namespace rt {
namespace {

/// FNV-1a accumulator; integer encodings always feed the least-significant byte first.
class Hash {
   public:
    void u32(uint32_t value) { bytes(value, 4); }
    void u64(uint64_t value) { bytes(value, 8); }
    void f32(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void vector(vec3 value) {
        f32(value.x);
        f32(value.y);
        f32(value.z);
    }
    uint64_t value() const { return value_; }

   private:
    void bytes(uint64_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            value_ ^= value & 0xffu;
            value_ *= 1099511628211ull;
            value >>= 8;
        }
    }
    uint64_t value_ = 14695981039346656037ull;
};

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Scene hashing requires IEEE-754 32-bit floats");

}  // namespace

uint64_t content_hash(const Scene& scene) {
    Hash hash;
    hash.u32(scene.num_spheres);
    hash.u32(scene.num_triangles);
    for (uint32_t i = 0; i < scene.num_spheres; ++i) {
        const Sphere& sphere = scene.spheres[i];
        hash.vector(sphere.center);
        hash.f32(sphere.radius);
        hash.u32(sphere.material_id);
    }
    for (uint32_t i = 0; i < scene.num_triangles; ++i) {
        const Triangle& triangle = scene.triangles[i];
        hash.vector(triangle.v0);
        hash.vector(triangle.v1);
        hash.vector(triangle.v2);
        hash.u32(triangle.material_id);
    }
    return hash.value();
}

uint64_t scene_hash(const Scene& scene) {
    Hash hash;
    hash.u64(content_hash(scene));
    hash.u32(scene.num_materials);
    for (uint32_t i = 0; i < scene.num_materials; ++i) {
        const Material& material = scene.materials[i];
        hash.vector(material.albedo);
        hash.vector(material.ks);
        hash.f32(material.shininess);
        hash.u32(material.flags);
    }
    hash.u32(scene.num_lights);
    for (uint32_t i = 0; i < scene.num_lights; ++i) {
        const Light& light = scene.lights[i];
        hash.u32(light.type);
        hash.vector(light.pos_or_dir);
        hash.vector(light.intensity);
    }
    hash.vector(scene.background);
    hash.f32(scene.ambient);
    return hash.value();
}

}  // namespace rt

/// @file scene.hpp
/// @brief Device-safe scene types and non-owning view shared by every backend.
#pragma once

#include <cstdint>
#include <type_traits>

#include "core/math.hpp"

namespace rt {

/// Sphere in right-handed, Y-up world coordinates.
struct Sphere {
    vec3 center;           ///< World-space center.
    float radius;          ///< Positive radius.
    uint32_t material_id;  ///< Index into the scene's materials.
};

/// Triangle; zero-area triangles are allowed and never hit.
struct Triangle {
    vec3 v0, v1, v2;       ///< World-space vertices in winding order.
    uint32_t material_id;  ///< Index into the scene's materials.
};

/// Material flags stored in scene files. Specular shading is an extension.
enum MaterialFlags : uint32_t {
    MAT_SPECULAR = 1u << 0,  ///< Enable the specular extension; bit 1 is reserved for mirrors.
};

/// Surface parameters; core shading uses only the diffuse albedo.
struct Material {
    color albedo;     ///< Nonnegative linear diffuse reflectance.
    color ks;         ///< Nonnegative linear specular reflectance (extension).
    float shininess;  ///< Specular exponent, at least 1.
    uint32_t flags;   ///< MaterialFlags bit set.
};

/// Light tags stored in scene files. Core loaders accept only point lights.
enum LightType : uint32_t {
    LIGHT_POINT = 0,        ///< Point light.
    LIGHT_DIRECTIONAL = 1,  ///< Directional light (extension).
};

/// Light parameters; padding is zero and excluded from scene hashes.
struct Light {
    uint32_t type;     ///< LightType tag.
    vec3 pos_or_dir;   ///< Point position or normalized directional-light direction.
    color intensity;   ///< Nonnegative linear RGB intensity.
    float pad = 0.0f;  ///< Reserved; written as zero in files.
};

/// Non-owning scene view passed by value to shared code and CUDA kernels.
///
/// Pointers refer to host or device storage as appropriate for the backend. Empty arrays
/// have a null pointer and count zero. The owner keeps the arrays alive and unmodified
/// while the view is in use. Scene IO validates all fields before rendering.
struct Scene {
    const Sphere* spheres;      ///< Sphere array.
    uint32_t num_spheres;       ///< Number of spheres.
    const Triangle* triangles;  ///< Triangle array.
    uint32_t num_triangles;     ///< Number of triangles.
    const Material* materials;  ///< Material array.
    uint32_t num_materials;     ///< Number of materials.
    const Light* lights;        ///< Light array.
    uint32_t num_lights;        ///< Number of lights.
    color background;           ///< Linear RGB color for missed primary rays.
    float ambient;              ///< Nonnegative ambient illumination.
};

static_assert(sizeof(Sphere) == 20, "Sphere must be 20 bytes");
static_assert(sizeof(Triangle) == 40, "Triangle must be 40 bytes");
static_assert(sizeof(Material) == 32, "Material must be 32 bytes");
static_assert(sizeof(Light) == 32, "Light must be 32 bytes");
static_assert(sizeof(Scene) == 80, "Scene must be 80 bytes on the supported 64-bit platforms");

static_assert(std::is_standard_layout<Sphere>::value && std::is_trivially_copyable<Sphere>::value,
              "Sphere must have a standard layout and be trivially copyable");
static_assert(std::is_standard_layout<Triangle>::value &&
                  std::is_trivially_copyable<Triangle>::value,
              "Triangle must have a standard layout and be trivially copyable");
static_assert(std::is_standard_layout<Material>::value &&
                  std::is_trivially_copyable<Material>::value,
              "Material must have a standard layout and be trivially copyable");
static_assert(std::is_standard_layout<Light>::value && std::is_trivially_copyable<Light>::value,
              "Light must have a standard layout and be trivially copyable");
static_assert(std::is_standard_layout<Scene>::value && std::is_trivial<Scene>::value,
              "Scene must remain a POD view");

}  // namespace rt

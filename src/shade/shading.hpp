/// @file shading.hpp
/// @brief Shared surface data and normal visualization shading.
#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "core/hostdev.hpp"
#include "core/ray.hpp"
#include "scene/scene.hpp"
#include "trace/hit.hpp"

namespace rt {

/// Geometric data at a hit; `ng` is unit length and faces against the incident ray.
struct SurfacePoint {
    vec3 p;                ///< World-space hit position.
    vec3 ng;               ///< Face-forwarded geometric normal.
    uint32_t material_id;  ///< Index into the scene's materials.
};

static_assert(sizeof(SurfacePoint) == 28, "SurfacePoint must be 28 bytes");
static_assert(std::is_standard_layout<SurfacePoint>::value &&
                  std::is_trivially_copyable<SurfacePoint>::value,
              "SurfacePoint must have a standard layout and be trivially copyable");

/// Per-pixel color and auxiliary primitive and light identifiers.
struct PixelResult {
    color rgb;            ///< Linear RGB result before clamping.
    uint32_t prim_id;     ///< Hit primitive, or `RT_MISS_PRIM`.
    uint32_t light_mask;  ///< Bits for contributing lights; zero for normal shading.
};

static_assert(sizeof(PixelResult) == 20, "PixelResult must be 20 bytes");
static_assert(std::is_standard_layout<PixelResult>::value &&
                  std::is_trivially_copyable<PixelResult>::value,
              "PixelResult must have a standard layout and be trivially copyable");

/// Computes the hit position and face-forwarded geometric normal.
/// @pre h.prim != RT_MISS_PRIM
/// @pre h.prim must identify a valid primitive in the scene.
RT_INLINE SurfacePoint surface_at(const Ray& r, const Hit& h, const Scene& s) {
    const bool is_triangle = (h.prim & PRIM_TRIANGLE_BIT) != 0u;
    vec3 p;
    vec3 ng;
    uint32_t material_id;
    if (is_triangle) {
        const Triangle& tri = s.triangles[h.prim & ~PRIM_TRIANGLE_BIT];
        p = (1.0f - h.u - h.v) * tri.v0 + h.u * tri.v1 + h.v * tri.v2;
        ng = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
        material_id = tri.material_id;
    } else {
        const Sphere& sphere = s.spheres[h.prim];
        p = r.origin + h.t * r.dir;
        ng = normalize(p - sphere.center);
        material_id = sphere.material_id;
    }
    if (dot(ng, r.dir) > 0.0f) ng = -ng;
    return {p, ng, material_id};
}

/// Reinterprets the bits of a float as a signed integer.
RT_INLINE int32_t float_bits(float f) {
#ifdef __CUDA_ARCH__
    return __float_as_int(f);
#else
    int32_t i;
    std::memcpy(&i, &f, sizeof(i));
    return i;
#endif
}

/// Reinterprets the bits of a signed integer as a float.
RT_INLINE float bits_float(int32_t i) {
#ifdef __CUDA_ARCH__
    return __int_as_float(i);
#else
    float f;
    std::memcpy(&f, &i, sizeof(f));
    return f;
#endif
}

/// Moves the point @p p off its surface along the unit normal @p ng, so a ray that starts
/// there does not hit the surface it leaves (Ray Tracing Gems I, Ch. 6).
///
/// Away from the origin the offset is a few units in the last place, applied to the integer
/// representation of each coordinate, so it scales with the coordinate's magnitude. Within
/// `1/32` of zero, where that scaling is too small, a fixed offset of `2^-16` is used.
/// @pre @p p is finite and @p ng has unit length.
/// @return The offset point, on the side @p ng points to.
RT_INLINE vec3 offset_ray_origin(vec3 p, vec3 ng) {
    constexpr float kOrigin = 1.0f / 32.0f;
    constexpr float kFloatScale = 1.0f / 65536.0f;
    constexpr float kIntScale = 256.0f;
    vec3 out;
    for (int a = 0; a < 3; ++a) {
        const float c = p[a];
        const float n = ng[a];
        const int32_t step = static_cast<int32_t>(kIntScale * n);
        const float moved = bits_float(float_bits(c) + (c < 0.0f ? -step : step));
        out[a] = ::fabsf(c) < kOrigin ? c + kFloatScale * n : moved;
    }
    return out;
}

/// Visualizes face-forwarded geometric normals without querying lights or tracing shadows.
struct NormalShading {
    template <class Q>
    RT_INLINE PixelResult operator()(const Ray& r, const Hit& h, const Scene& s, const Q&) const {
        if (h.prim == RT_MISS_PRIM) return {s.background, RT_MISS_PRIM, 0u};
        const vec3 n = surface_at(r, h, s).ng;
        return {0.5f * (n + vec3{1.0f, 1.0f, 1.0f}), h.prim, 0u};
    }
};

}  // namespace rt

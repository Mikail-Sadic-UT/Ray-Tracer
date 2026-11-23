/// @file shading.hpp
/// @brief Shared surface data and normal visualization shading.
#pragma once

#include <cfloat>
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

/// `x^n` for the specular lobe, computed the same way on every backend (E2, IC4).
///
/// `powf` differs between glibc and CUDA in the last bits, which would break the byte equality
/// of CPU and GPU images. This uses only multiplication and `sqrtf`, both correctly rounded on
/// the host and the device, in a fixed order: the integer part of the exponent by repeated
/// squaring, then its first 16 fractional bits as a product of `x^(2^-j)` (repeated square
/// roots). Exponents of `2^24` and above are halved first and the result squared back. Relative
/// error is a few units in the last place plus at most `x^(2^-16)` from the truncated fraction.
/// @param x Base; values at or below 0 give 0 and values at or above 1 give 1.
/// @param n Exponent (`shininess`), finite and at least 1.
RT_INLINE float spec_pow(float x, float n) {
    if (!(x > 0.0f)) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    int squarings = 0;
    while (n >= 16777216.0f) {  // 2^24: past here floats have no fractional part
        n *= 0.5f;
        ++squarings;
    }
    const float whole = ::floorf(n);
    float frac = n - whole;  // exact
    uint32_t k = static_cast<uint32_t>(whole);
    float result = 1.0f;
    float base = x;
    while (k != 0u) {
        if ((k & 1u) != 0u) result *= base;
        k >>= 1;
        if (k != 0u) base *= base;
    }
    float root = x;
    for (int j = 0; j < 16 && frac > 0.0f; ++j) {
        root = ::sqrtf(root);
        frac *= 2.0f;  // exact
        if (frac >= 1.0f) {
            result *= root;
            frac -= 1.0f;
        }
    }
    for (int j = 0; j < squarings; ++j) result *= result;
    return result;
}

/// Evaluates light @p i at surface point @p sp and builds its shadow ray (INTERFACES section 6,
/// "Shadow-ray construction" and "Shading model").
///
/// Lighting terms use the unit direction `l` and squared distance `d2` from `sp.p`; the shadow
/// ray starts at the offset origin. A point light uses `l = normalize(pos - p)` and its shadow
/// ray ends at the light. A directional light (E2) uses `l = normalize(-dir)` and `d2 = 1`, and
/// its shadow ray is unbounded. Any other light type contributes nothing.
/// @param sp          Surface point, with a unit normal that faces the viewer.
/// @param wo          Unit direction to the viewer; used by `MAT_SPECULAR` materials.
/// @param s           Scene view.
/// @param i           Light index, below `s.num_lights`.
/// @param[out] contrib      `albedo * max(0, dot(ng, l)) * intensity / d2`, plus, for
///                          `MAT_SPECULAR`, `ks * max(0, dot(ng, h))^shininess * intensity / d2`
///                          with `h = normalize(l + wo)`; set only on success.
/// @param[out] shadow_ray   Ray from the offset origin toward the light, `tmin = 0`; set only on
///                          success.
/// @return False when the light adds nothing and needs no shadow ray: a point light that
///         coincides with the point (`d2` zero or not normal), a light behind the surface, or
///         an unknown light type.
RT_INLINE bool light_sample(const SurfacePoint& sp, vec3 wo, const Scene& s, uint32_t i,
                            color& contrib, Ray& shadow_ray) {
    const Light& light = s.lights[i];
    vec3 l;
    float d2;
    if (light.type == LIGHT_POINT) {
        const vec3 to_light = light.pos_or_dir - sp.p;
        d2 = dot(to_light, to_light);
        if (!(d2 >= FLT_MIN && d2 <= FLT_MAX)) return false;
        l = to_light / ::sqrtf(d2);
    } else if (light.type == LIGHT_DIRECTIONAL) {
        l = normalize(-light.pos_or_dir);
        d2 = 1.0f;
    } else {
        return false;
    }
    const float cos_theta = dot(sp.ng, l);
    if (cos_theta <= 0.0f) return false;
    const vec3 origin = offset_ray_origin(sp.p, sp.ng);
    if (light.type == LIGHT_POINT) {
        const vec3 to_light_from_origin = light.pos_or_dir - origin;
        const float o2 = dot(to_light_from_origin, to_light_from_origin);
        if (!(o2 >= FLT_MIN && o2 <= FLT_MAX)) return false;
        shadow_ray = {origin, 0.0f, to_light_from_origin / ::sqrtf(o2), ::sqrtf(o2)};
    } else {
        shadow_ray = {origin, 0.0f, l, INFINITY};
    }
    const Material& material = s.materials[sp.material_id];
    contrib = material.albedo * cos_theta * light.intensity / d2;
    if ((material.flags & MAT_SPECULAR) != 0u) {
        const vec3 h = normalize(l + wo);
        const float spec = spec_pow(dot(sp.ng, h), material.shininess);
        contrib += material.ks * spec * light.intensity / d2;
    }
    return true;
}

/// Shades a primary hit with Lambert lighting, Blinn-Phong highlights for `MAT_SPECULAR`
/// materials, and hard shadows (INTERFACES section 6).
///
/// A miss returns the background color, `RT_MISS_PRIM` and a zero mask. A hit starts from
/// `albedo * ambient`, then adds each light in index order whose shadow ray is unoccluded
/// according to `q.any`, setting bit `i` of `light_mask` for it (lights `i < 32`). The order is
/// fixed so every backend sums the same floats in the same sequence.
/// @tparam Query Provides `bool any(const Ray&) const`.
/// @pre `h` is a miss or identifies a primitive of `s`.
template <class Query>
RT_INLINE PixelResult shade_hit(const Ray& r, const Hit& h, const Scene& s, const Query& q) {
    if (h.prim == RT_MISS_PRIM) return {s.background, RT_MISS_PRIM, 0u};
    const SurfacePoint sp = surface_at(r, h, s);
    const vec3 wo = -r.dir;
    color rgb = s.materials[sp.material_id].albedo * s.ambient;
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < s.num_lights; ++i) {
        color contrib;
        Ray shadow_ray;
        if (light_sample(sp, wo, s, i, contrib, shadow_ray) && !q.any(shadow_ray)) {
            rgb += contrib;
            if (i < 32u) mask |= (1u << i);
        }
    }
    return {rgb, h.prim, mask};
}

/// Lambert (plus Blinn-Phong for `MAT_SPECULAR`) shading with point and directional lights and
/// hard shadows; the default policy for `render_pixel`.
struct LitShading {
    template <class Q>
    RT_INLINE PixelResult operator()(const Ray& r, const Hit& h, const Scene& s, const Q& q) const {
        return shade_hit(r, h, s, q);
    }
};

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

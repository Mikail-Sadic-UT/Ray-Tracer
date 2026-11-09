/// @file render_pixel.hpp
/// @brief Shared per-pixel entry point called by every backend (INTERFACES §6, "Pixel result").
#pragma once

#include <cstdint>

#include "camera/camera.hpp"
#include "core/hostdev.hpp"
#include "core/ray.hpp"
#include "scene/scene.hpp"
#include "shade/shading.hpp"
#include "trace/hit.hpp"

namespace rt {

/// Renders pixel `(px, py)`: builds the camera ray, finds its closest hit with @p q, and
/// shades that hit with @p shade.
///
/// Color and both AOVs come from the same primary hit, so nothing is retraced. Both template
/// parameters are compile-time policies: `Query` provides `closest(const Ray&)` and
/// `any(const Ray&)` (BruteQuery, BvhQuery), and `Shading` is called as
/// `shade(ray, hit, scene, q)` (NormalShading, LitShading). The CPU backend calls this per
/// pixel; the CUDA megakernel calls it once per thread.
///
/// @note INTERFACES §6 defaults `Shading` to `LitShading`. That default is added together with
/// `LitShading` (`shade_hit`, ROADMAP C4). Until then, name the policy
/// (`render_pixel<BruteQuery, NormalShading>(...)`) or pass it as @p shade so it is deduced.
///
/// @param cam   Camera; `px < cam.width` and `py < cam.height`.
/// @param px    Pixel column.
/// @param py    Pixel row, with row zero at the top.
/// @param s     Scene view, in memory the caller can read (host or device).
/// @param q     Tracing query over the same scene.
/// @param shade Shading policy.
/// @return The pixel's linear color, `prim_id` and `light_mask`.
template <class Query, class Shading>
RT_INLINE PixelResult render_pixel(const Camera& cam, uint32_t px, uint32_t py, const Scene& s,
                                   const Query& q, Shading shade = {}) {
    const Ray r = camera_ray(cam, px, py);
    const Hit h = q.closest(r);
    return shade(r, h, s, q);
}

}  // namespace rt

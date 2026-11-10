/// @file backend.hpp
/// @brief Backend interface shared by every renderer (CPU, CUDA, Embree, OptiX), INTERFACES §7.
///
/// Host-only. `rt_render` resolves the effective camera and RenderSettings, builds our BVH when
/// needs_bvh() is true and `--bvh` isn't `none`, then calls prepare() once per preparation and
/// render() once per frame.
#pragma once

#include <cstdint>

#include "bench/timings.hpp"
#include "bvh/bvh.hpp"
#include "camera/camera.hpp"
#include "image/image.hpp"
#include "scene/scene_host.hpp"

namespace rt {

/// Requested auxiliary outputs (bit set in RenderSettings::aovs).
enum AovFlags : uint32_t {
    AOV_PRIM_ID = 1u << 0,     ///< Accepted primary-hit PrimRef per pixel.
    AOV_LIGHT_MASK = 1u << 1,  ///< Contributing-light bit mask per pixel.
};

/// Shading policy selected by `--shade lit|normal`.
enum class ShadeMode : uint32_t {
    Lit = 0,     ///< LitShading: Lambert and shadow rays.
    Normal = 1,  ///< NormalShading: face-forwarded geometric normal, no lights.
};

/// Effective render settings, after JSON values and CLI overrides.
struct RenderSettings {
    uint32_t width, height;  ///< Fixed for the lifetime of one preparation.
    uint32_t aovs;           ///< AovFlags.
    ShadeMode shade;         ///< Selects the Shading policy.
};

/// Where scene and BVH arrays live on the GPU (`--mem`). Explicit is Core; the rest is E1.
enum class MemMode : uint32_t {
    Explicit = 0,     ///< `cudaMalloc` + `cudaMemcpy` per array.
    Managed = 1,      ///< `cudaMallocManaged`, CPU writes first.
    System = 2,       ///< Fresh host pages per preparation, read by the GPU over NVLink-C2C.
    SystemReuse = 3,  ///< Views into the persistent SceneHost / Bvh (separate experiment).
};

/// `cudaMemAdvise` applied to managed scene arrays (`--mem-advice`).
enum class MemAdvice : uint32_t {
    None = 0,          ///< No advice.
    PreferredGpu = 1,  ///< `cudaMemAdviseSetPreferredLocation` on the device.
    ReadMostly = 2,    ///< `cudaMemAdviseSetReadMostly`.
};

/// GPU memory configuration (`--mem`, `--mem-advice`, `--prefetch`). CUDA only; other backends
/// ignore it.
struct MemConfig {
    MemMode mode = MemMode::Explicit;
    MemAdvice advice = MemAdvice::None;
    bool prefetch = false;  ///< `cudaMemPrefetchAsync` managed arrays to the device in prepare.
};

/// Everything a backend needs for one preparation.
struct PrepareInput {
    const SceneHost* scene;  ///< Must outlive the backend's preparation.
    const Bvh* bvh;          ///< Our BVH over @p scene; nullptr if !needs_bvh() or `--bvh none`.
    RenderSettings render;   ///< Effective resolution, AOVs, and shading.
    int build_threads;       ///< B: preparation threads.
    int render_threads;      ///< R: render threads (CPU backends).
    MemConfig mem;           ///< GPU memory mode (CUDA only).
};

/// A renderer. One instance holds at most one preparation at a time.
class Backend {
   public:
    virtual ~Backend() = default;

    /// True if the backend traverses our Bvh (cpu, cuda); false if it builds its own (embree,
    /// optix).
    virtual bool needs_bvh() const = 0;

    /// Prepares to render @p in: releases any previous preparation first (untimed), then
    /// allocates per-frame buffers (`alloc`), uploads (`upload`), or builds (`convert`).
    /// @throws Error if the input is invalid or unsupported by this backend.
    virtual void prepare(const PrepareInput& in, Timings& t) = 0;

    /// Renders one frame into host memory. Repeatable; never allocates device memory.
    /// Fills `render` (and `render_kernel`, `download` where they apply).
    /// @pre prepare() has succeeded.
    /// @throws Error if not prepared, if `cam.width/height` differ from the prepared resolution,
    ///         or on a runtime failure such as a traversal stack overflow.
    virtual void render(const Camera& cam, Image& out, Timings& t) = 0;

    /// Frees all resources of the current preparation. Idempotent.
    virtual void release() = 0;

    /// Backend name as given to `--backend`.
    virtual const char* name() const = 0;
};

}  // namespace rt

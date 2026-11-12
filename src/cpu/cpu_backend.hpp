/// @file cpu_backend.hpp
/// @brief CPU backend: the multicore reference renderer behind `rt_render --backend cpu`.
#pragma once

#include "backend.hpp"
#include "bvh/bvh_types.hpp"
#include "scene/scene.hpp"

namespace rt {

/// Renders on the host with OpenMP over dynamic 16x16 tiles (`num_threads(R)`), calling
/// `render_pixel` per pixel. Output is bit-identical for every render thread count.
///
/// The query is chosen in prepare(): `BvhQuery` semantics when PrepareInput::bvh is set,
/// `BruteQuery` otherwise (`--bvh none`). The Shading policy follows RenderSettings::shade:
/// `LitShading` (Lambert and shadow rays through the same query) or `NormalShading`. Traversal
/// reports stack overflow through a flag that render() turns into an Error (INTERFACES §5; see
/// CONTRACT_NOTES W8).
///
/// CPU preparation does no timed work: the BVH is built by the caller, and the image is host
/// memory resized in render(). render() fills Timings::render only; there is no download.
class CpuBackend final : public Backend {
   public:
    bool needs_bvh() const override { return true; }

    /// Stores views of the scene and BVH and the render settings.
    /// @throws Error if the scene is null, the thread count or resolution is not positive, the
    ///         AOV flags are unknown, a light-mask AOV is requested with more than 32 lights,
    ///         the BVH doesn't cover the scene's primitives, or `shade` is not a known mode.
    void prepare(const PrepareInput& in, Timings& t) override;

    /// Renders @p cam into @p out (resized to the prepared resolution; unrequested AOVs are
    /// cleared). Sets `t.render`.
    /// @throws Error if not prepared, if the camera resolution differs from the prepared one,
    ///         or if traversal overflowed its stack (invalid BVH).
    void render(const Camera& cam, Image& out, Timings& t) override;

    void release() override;

    const char* name() const override { return "cpu"; }

   private:
    bool prepared_ = false;
    Scene scene_{};
    BvhView bvh_{nullptr, nullptr, 0};
    bool use_bvh_ = false;
    RenderSettings settings_{};
    int threads_ = 1;
};

}  // namespace rt

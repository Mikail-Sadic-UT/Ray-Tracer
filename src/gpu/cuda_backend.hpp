/// @file cuda_backend.hpp
/// @brief CUDA megakernel backend behind `rt_render --backend cuda` (ROADMAP C10).
///
/// Host-only. The header needs no CUDA headers; the kernel and the lifecycle are in
/// cuda_backend.cu (rt_gpu).
#pragma once
#include <cstdint>

#include "backend.hpp"
#include "gpu/device_scene.hpp"
namespace rt {
/// Renders on the GPU with one thread per pixel calling `render_pixel` (the megakernel).
///
/// prepare() uploads the scene and BVH through a DeviceScene (explicit memory), and allocates
/// the framebuffer, the requested AOV buffers, and the traversal overflow flag. render() never
/// allocates: it launches the kernel and downloads the image and AOVs into host memory.
///
/// The query is chosen in prepare(): BVH traversal when PrepareInput::bvh is set, brute force
/// otherwise (`--bvh none`). The Shading policy follows RenderSettings::shade. Traversal
/// reports a stack overflow through a device flag that render() turns into an Error
/// (INTERFACES §5; see CONTRACT_NOTES W8). Each pixel writes only its own slots and color has
/// no atomics, so repeated renders are bit-identical.
///
/// Timings: prepare() sets `alloc` and `upload`; render() sets `render` (launch to kernel
/// completion, host wall clock), `render_kernel` (cudaEvent, overlaps `render`) and
/// `download`.
class CudaBackend final : public Backend {
   public:
    CudaBackend() = default;
    CudaBackend(const CudaBackend&) = delete;
    CudaBackend& operator=(const CudaBackend&) = delete;
    /// Frees the device resources (errors are ignored here; call release() to see them).
    ~CudaBackend() override;
    bool needs_bvh() const override { return true; }
    /// Uploads the scene and BVH and allocates the per-frame buffers.
    /// @throws Error if the scene is null, the resolution is not positive, the AOV flags are
    ///         unknown, a light-mask AOV is requested with more than 32 lights, the BVH doesn't
    ///         cover the scene's primitives, `shade` is not a known mode, the memory mode is not
    ///         explicit, or a CUDA call fails. A failed prepare leaves the backend released.
    void prepare(const PrepareInput& in, Timings& t) override;
    /// Renders @p cam into @p out (resized to the prepared resolution; unrequested AOVs are
    /// cleared).
    /// @throws Error if not prepared, if the camera resolution differs from the prepared one,
    ///         if traversal overflowed its stack (invalid BVH), or if a CUDA call fails.
    void render(const Camera& cam, Image& out, Timings& t) override;
    void release() override;
    const char* name() const override { return "cuda"; }

   private:
    bool prepared_ = false;
    DeviceScene scene_;
    bool use_bvh_ = false;
    RenderSettings settings_{};
    float* d_rgb_ = nullptr;            ///< 3 floats per pixel, row-major, row 0 = top.
    uint32_t* d_prim_id_ = nullptr;     ///< Only when AOV_PRIM_ID is requested.
    uint32_t* d_light_mask_ = nullptr;  ///< Only when AOV_LIGHT_MASK is requested.
    uint32_t* d_overflow_ = nullptr;    ///< Traversal overflow flag, cleared before each frame.
};
}  // namespace rt

/// @file cuda_backend.cu
/// @brief CUDA backend lifecycle, the megakernel, and its (query, shading) dispatch.
#include "gpu/cuda_backend.hpp"
// Other headers follow.
#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "bench/gpu_timer.hpp"
#include "core/error.hpp"
#include "gpu/cuda_check.hpp"
#include "shade/render_pixel.hpp"
#include "shade/shading.hpp"
#include "trace/brute.hpp"
#include "trace/traverse.hpp"
namespace rt {
namespace {
/// Thread block of the megakernel: 16x16 pixels, as the CPU tiles. Tuning is G3 work.
constexpr uint32_t kBlockX = 16;
constexpr uint32_t kBlockY = 16;

/// BvhQuery plus the device overflow flag. §6's BvhQuery has no member for the flag
/// (CONTRACT_NOTES W8), so the backend keeps its own query type with the same two calls, as
/// CpuBackend does.
struct CheckedBvhQuery {
    Scene s;
    BvhView bvh;
    uint32_t* overflow;  ///< One device word shared by all threads; set with atomicOr.
    RT_INLINE Hit closest(const Ray& r) const { return traverse_closest(r, s, bvh, overflow); }
    RT_INLINE bool any(const Ray& r) const { return traverse_any(r, s, bvh, overflow); }
};

/// The megakernel: one thread per pixel, writing its own PixelResult fields. AOV pointers are
/// null when the AOV was not requested.
template <class Query, class Shading>
__global__ void megakernel(Camera cam, Scene s, Query q, float* rgb, uint32_t* prim_id,
                           uint32_t* light_mask) {
    const uint32_t px = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= cam.width || py >= cam.height) return;
    const PixelResult p = render_pixel<Query, Shading>(cam, px, py, s, q, Shading{});
    const size_t i = static_cast<size_t>(py) * cam.width + px;
    rgb[3 * i] = p.rgb.x;
    rgb[3 * i + 1] = p.rgb.y;
    rgb[3 * i + 2] = p.rgb.z;
    if (prim_id != nullptr) prim_id[i] = p.prim_id;
    if (light_mask != nullptr) light_mask[i] = p.light_mask;
}

/// Launches the megakernel with the Shading policy that @p mode selects. prepare() has already
/// rejected any other mode.
template <class Query>
void launch(const Camera& cam, const Scene& s, const Query& q, ShadeMode mode, float* rgb,
            uint32_t* prim_id, uint32_t* light_mask) {
    const dim3 block(kBlockX, kBlockY);
    const dim3 grid((cam.width + kBlockX - 1) / kBlockX, (cam.height + kBlockY - 1) / kBlockY);
    if (mode == ShadeMode::Lit) {
        megakernel<Query, LitShading><<<grid, block>>>(cam, s, q, rgb, prim_id, light_mask);
    } else {
        megakernel<Query, NormalShading><<<grid, block>>>(cam, s, q, rgb, prim_id, light_mask);
    }
    RT_CUDA_CHECK(cudaGetLastError());
}

/// Frees @p d if set and clears it, keeping the first error.
template <class T>
void free_buffer(T*& d, cudaError_t& first) {
    if (d != nullptr) {
        const cudaError_t err = cudaFree(d);
        if (first == cudaSuccess) first = err;
        d = nullptr;
    }
}
}  // namespace

CudaBackend::~CudaBackend() {
    try {
        release();
    } catch (...) {
    }
}

void CudaBackend::prepare(const PrepareInput& in, Timings& t) {
    release();
    if (in.scene == nullptr) throw Error("cuda: PrepareInput::scene is null");
    const RenderSettings& rs = in.render;
    if (rs.width == 0 || rs.height == 0)
        throw Error("cuda: render width and height must be positive");
    if ((rs.aovs & ~(AOV_PRIM_ID | AOV_LIGHT_MASK)) != 0) {
        throw Error("cuda: unknown AOV flags 0x" + std::to_string(rs.aovs));
    }
    if ((rs.aovs & AOV_LIGHT_MASK) && in.scene->lights.size() > 32) {
        throw Error("cuda: the lightmask AOV supports at most 32 lights, scene has " +
                    std::to_string(in.scene->lights.size()));
    }
    if (rs.shade != ShadeMode::Lit && rs.shade != ShadeMode::Normal) {
        throw Error("cuda: unknown shade mode " + std::to_string(static_cast<uint32_t>(rs.shade)));
    }
    if (in.bvh != nullptr) {
        const size_t prims = in.scene->spheres.size() + in.scene->triangles.size();
        if (in.bvh->prim_refs.size() != prims || in.bvh->nodes.empty() != (prims == 0)) {
            throw Error("cuda: the BVH has " + std::to_string(in.bvh->prim_refs.size()) +
                        " primitive references but the scene has " + std::to_string(prims) +
                        " primitives");
        }
    }
    try {
        scene_.upload(*in.scene, in.bvh, in.mem, t);
        // Per-frame buffers: framebuffer, requested AOVs, overflow flag. Added to `alloc`.
        double buffers_ms = 0.0;
        {
            CudaStageTimer alloc(buffers_ms);
            const size_t pixels = static_cast<size_t>(rs.width) * rs.height;
            RT_CUDA_CHECK(cudaMalloc(&d_rgb_, pixels * 3 * sizeof(float)));
            if (rs.aovs & AOV_PRIM_ID) {
                RT_CUDA_CHECK(cudaMalloc(&d_prim_id_, pixels * sizeof(uint32_t)));
            }
            if (rs.aovs & AOV_LIGHT_MASK) {
                RT_CUDA_CHECK(cudaMalloc(&d_light_mask_, pixels * sizeof(uint32_t)));
            }
            RT_CUDA_CHECK(cudaMalloc(&d_overflow_, sizeof(uint32_t)));
            alloc.stop();
        }
        t.alloc += buffers_ms;
    } catch (...) {
        try {
            release();
        } catch (...) {
        }
        throw;
    }
    use_bvh_ = in.bvh != nullptr;
    settings_ = rs;
    prepared_ = true;
}

void CudaBackend::render(const Camera& cam, Image& out, Timings& t) {
    if (!prepared_) throw Error("cuda: render called before prepare");
    if (cam.width != settings_.width || cam.height != settings_.height) {
        throw Error("cuda: camera resolution " + std::to_string(cam.width) + "x" +
                    std::to_string(cam.height) + " differs from the prepared " +
                    std::to_string(settings_.width) + "x" + std::to_string(settings_.height) +
                    "; call prepare again");
    }
    const size_t pixels = static_cast<size_t>(cam.width) * cam.height;
    out.width = cam.width;
    out.height = cam.height;
    out.rgb.resize(pixels * 3);
    out.prim_id.resize(d_prim_id_ != nullptr ? pixels : 0);
    out.light_mask.resize(d_light_mask_ != nullptr ? pixels : 0);

    const Scene s = scene_.scene_view();
    CudaEventTimer kernel;
    {
        CudaStageTimer render(t.render);
        RT_CUDA_CHECK(cudaMemset(d_overflow_, 0, sizeof(uint32_t)));
        kernel.start();
        if (use_bvh_) {
            launch(cam, s, CheckedBvhQuery{s, scene_.bvh_view(), d_overflow_}, settings_.shade,
                   d_rgb_, d_prim_id_, d_light_mask_);
        } else {
            launch(cam, s, BruteQuery{s}, settings_.shade, d_rgb_, d_prim_id_, d_light_mask_);
        }
        kernel.stop();
        render.stop();  // synchronizes, so kernel faults surface here
    }
    t.render_kernel = kernel.elapsed_ms();

    uint32_t overflow = 0;
    {
        CudaStageTimer download(t.download);
        RT_CUDA_CHECK(cudaMemcpy(out.rgb.data(), d_rgb_, out.rgb.size() * sizeof(float),
                                 cudaMemcpyDeviceToHost));
        if (d_prim_id_ != nullptr) {
            RT_CUDA_CHECK(cudaMemcpy(out.prim_id.data(), d_prim_id_, pixels * sizeof(uint32_t),
                                     cudaMemcpyDeviceToHost));
        }
        if (d_light_mask_ != nullptr) {
            RT_CUDA_CHECK(cudaMemcpy(out.light_mask.data(), d_light_mask_,
                                     pixels * sizeof(uint32_t), cudaMemcpyDeviceToHost));
        }
        RT_CUDA_CHECK(cudaMemcpy(&overflow, d_overflow_, sizeof(uint32_t), cudaMemcpyDeviceToHost));
        download.stop();
    }
    if (overflow != 0) {
        throw Error("cuda: BVH traversal stack overflow; the BVH is deeper than 64 levels");
    }
}

void CudaBackend::release() {
    prepared_ = false;
    use_bvh_ = false;
    settings_ = RenderSettings{};
    cudaError_t err = cudaSuccess;
    free_buffer(d_rgb_, err);
    free_buffer(d_prim_id_, err);
    free_buffer(d_light_mask_, err);
    free_buffer(d_overflow_, err);
    scene_.release();
    RT_CUDA_CHECK(err);
}
}  // namespace rt

/// @file device_scene.cu
/// @brief DeviceScene upload and release (explicit memory: `cudaMalloc` + `cudaMemcpy`).
#include "gpu/device_scene.hpp"
// Other headers follow.
#include <cuda_runtime.h>

#include <cstddef>
#include <vector>

#include "bench/gpu_timer.hpp"
#include "core/error.hpp"
#include "gpu/cuda_check.hpp"
namespace rt {
namespace {
/// Allocates room for @p v on the device, or sets @p d to null if @p v is empty.
template <class T>
void alloc_array(T*& d, const std::vector<T>& v) {
    d = nullptr;
    if (!v.empty()) RT_CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(T)));
}
/// Copies @p v into the device array @p d allocated by alloc_array().
template <class T>
void copy_array(T* d, const std::vector<T>& v) {
    if (!v.empty()) {
        RT_CUDA_CHECK(cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    }
}
/// Frees @p d if set and clears it. Returns the first error instead of throwing, so every
/// array is freed even if one cudaFree fails.
template <class T>
cudaError_t free_array(T*& d, cudaError_t first) {
    if (d != nullptr) {
        const cudaError_t err = cudaFree(d);
        if (first == cudaSuccess) first = err;
        d = nullptr;
    }
    return first;
}
}  // namespace

DeviceScene::~DeviceScene() {
    try {
        release();
    } catch (...) {
    }
}

void DeviceScene::upload(const SceneHost& scene, const Bvh* bvh, const MemConfig& mem, Timings& t) {
    release();
    if (mem.mode != MemMode::Explicit) {
        throw Error(
            "gpu: only --mem explicit is implemented; managed and system memory modes "
            "are Extension E1");
    }
    try {
        {
            CudaStageTimer alloc(t.alloc);
            alloc_array(spheres_, scene.spheres);
            alloc_array(triangles_, scene.triangles);
            alloc_array(materials_, scene.materials);
            alloc_array(lights_, scene.lights);
            if (bvh != nullptr) {
                alloc_array(nodes_, bvh->nodes);
                alloc_array(prim_refs_, bvh->prim_refs);
            }
            alloc.stop();
        }
        {
            CudaStageTimer copy(t.upload);
            copy_array(spheres_, scene.spheres);
            copy_array(triangles_, scene.triangles);
            copy_array(materials_, scene.materials);
            copy_array(lights_, scene.lights);
            if (bvh != nullptr) {
                copy_array(nodes_, bvh->nodes);
                copy_array(prim_refs_, bvh->prim_refs);
            }
            copy.stop();
        }
    } catch (...) {
        try {
            release();
        } catch (...) {
        }
        throw;
    }
    view_ = {spheres_,         static_cast<uint32_t>(scene.spheres.size()),
             triangles_,       static_cast<uint32_t>(scene.triangles.size()),
             materials_,       static_cast<uint32_t>(scene.materials.size()),
             lights_,          static_cast<uint32_t>(scene.lights.size()),
             scene.background, scene.ambient};
    if (bvh != nullptr) bvh_ = {nodes_, prim_refs_, static_cast<uint32_t>(bvh->nodes.size())};
}

void DeviceScene::release() {
    cudaError_t err = cudaSuccess;
    err = free_array(spheres_, err);
    err = free_array(triangles_, err);
    err = free_array(materials_, err);
    err = free_array(lights_, err);
    err = free_array(nodes_, err);
    err = free_array(prim_refs_, err);
    view_ = Scene{};
    bvh_ = {nullptr, nullptr, 0};
    RT_CUDA_CHECK(err);
}
}  // namespace rt

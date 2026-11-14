/// @file device_scene.hpp
/// @brief DeviceScene: device copies of the scene and BVH arrays (INTERFACES §2 "Owning storage
/// vs views").
///
/// Host-only. The header needs no CUDA headers, so host C++ code can hold a DeviceScene; the
/// implementation is in device_scene.cu (rt_gpu).
#pragma once
#include "backend.hpp"
#include "bvh/bvh.hpp"
#include "bvh/bvh_types.hpp"
#include "scene/scene.hpp"
#include "scene/scene_host.hpp"
namespace rt {
/// Owns the device copies of a scene and, optionally, its BVH.
///
/// upload() makes one allocation and one copy per non-empty array: spheres, triangles,
/// materials, lights, bvh.nodes, bvh.prim_refs. Empty arrays get a null pointer and count 0.
/// The views returned by scene_view() and bvh_view() hold device pointers and are passed by
/// value to kernels.
class DeviceScene {
   public:
    DeviceScene() = default;
    DeviceScene(const DeviceScene&) = delete;
    DeviceScene& operator=(const DeviceScene&) = delete;
    /// Frees the device arrays (errors are ignored here; call release() to see them).
    ~DeviceScene();
    /// Releases any previous upload (untimed), then allocates and copies every non-empty array.
    /// Sets `t.alloc` (all `cudaMalloc` calls) and `t.upload` (all `cudaMemcpy` calls), each
    /// measured with `cudaDeviceSynchronize` at both ends.
    /// @param bvh Our BVH over @p scene, or nullptr for brute force (`--bvh none`).
    /// @throws Error if @p mem is not MemMode::Explicit (managed and system modes are E1), or
    ///         on a CUDA failure. A failed upload leaves the object released.
    void upload(const SceneHost& scene, const Bvh* bvh, const MemConfig& mem, Timings& t);
    /// Device-pointer view of the scene. Null pointers and zero counts before upload().
    Scene scene_view() const { return view_; }
    /// Device-pointer view of the BVH; `num_nodes == 0` if no BVH was uploaded.
    BvhView bvh_view() const { return bvh_; }
    /// Frees every device array. Idempotent.
    /// @throws Error if `cudaFree` fails; the object is released regardless.
    void release();

   private:
    Sphere* spheres_ = nullptr;
    Triangle* triangles_ = nullptr;
    Material* materials_ = nullptr;
    Light* lights_ = nullptr;
    BvhNode* nodes_ = nullptr;
    PrimRef* prim_refs_ = nullptr;
    Scene view_{};
    BvhView bvh_{nullptr, nullptr, 0};
};
}  // namespace rt

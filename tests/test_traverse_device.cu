/// @file test_traverse_device.cu
/// @brief Runs BVH traversal in CUDA kernels: BvhQuery against BruteQuery, and the overflow flag.
///
/// Built only when RT_ENABLE_CUDA is on. Cases that need a GPU skip when no CUDA device is
/// visible (RT_SKIP_WITHOUT_CUDA_DEVICE); host-only fixture checks always run.
// traverse.hpp comes first, so this file also checks that it compiles as CUDA on its own.
#include "trace/traverse.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "bench/timings.hpp"
#include "bvh/bvh.hpp"
#include "cuda_test_util.hpp"
#include "gpu/cuda_check.hpp"
#include "trace/brute.hpp"

namespace {

/// Copies @p v to a new device array (null for an empty vector). The caller frees it.
template <class T>
T* to_device(const std::vector<T>& v) {
    if (v.empty()) return nullptr;
    T* d = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(T)));
    RT_CUDA_CHECK(cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    return d;
}

/// Copies @p n elements of device array @p d back to the host.
template <class T>
std::vector<T> to_host(const T* d, size_t n) {
    std::vector<T> v(n);
    RT_CUDA_CHECK(cudaMemcpy(v.data(), d, n * sizeof(T), cudaMemcpyDeviceToHost));
    return v;
}

/// Waits for the last kernel and turns a launch or execution error into rt::Error.
void sync_kernel() {
    RT_CUDA_CHECK(cudaGetLastError());
    RT_CUDA_CHECK(cudaDeviceSynchronize());
}

/// Scene view over arrays in host or device memory; materials and lights are unused.
rt::Scene make_scene(const rt::Sphere* spheres, uint32_t ns, const rt::Triangle* tris,
                     uint32_t nt) {
    rt::Scene s{};
    s.spheres = spheres;
    s.num_spheres = ns;
    s.triangles = tris;
    s.num_triangles = nt;
    return s;
}

/// One ray's results from both queries, computed in the same kernel.
struct QueryResult {
    rt::Hit bvh_closest, brute_closest;
    int bvh_any, brute_any;
};

/// One thread per ray: BvhQuery and BruteQuery on the same device scene.
__global__ void query_kernel(const rt::Ray* rays, int n, rt::BvhQuery bq, rt::BruteQuery fq,
                             uint32_t* overflow, QueryResult* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    QueryResult q;
    q.bvh_closest = rt::traverse_closest(rays[i], bq.s, bq.bvh, overflow);
    q.bvh_any = rt::traverse_any(rays[i], bq.s, bq.bvh, overflow);
    q.brute_closest = fq.closest(rays[i]);
    q.brute_any = fq.any(rays[i]);
    // BvhQuery's members must forward to the same functions.
    const rt::Hit via_query = bq.closest(rays[i]);
    const rt::Hit& h = q.bvh_closest;
    if (via_query.t != h.t || via_query.prim != h.prim || via_query.u != h.u ||
        via_query.v != h.v || bq.any(rays[i]) != static_cast<bool>(q.bvh_any)) {
        q.bvh_any = -1;
    }
    out[i] = q;
}

float uniform(std::mt19937& rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

rt::vec3 uniform_vec(std::mt19937& rng, float h) {
    return {uniform(rng, -h, h), uniform(rng, -h, h), uniform(rng, -h, h)};
}

/// In a kernel, BVH traversal returns exactly the brute-force hit (same prim, bit-identical t,
/// u, v) on a random scene with duplicated primitives, for several leaf sizes.
TEST(TraverseDevice, MatchesBruteForceOnDevice) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    std::mt19937 rng(301);
    std::vector<rt::Sphere> spheres;
    std::vector<rt::Triangle> tris;
    for (int i = 0; i < 300; ++i) {
        if (i > 0 && rng() % 6u == 0) {
            spheres.push_back(spheres[rng() % spheres.size()]);
        } else {
            spheres.push_back({uniform_vec(rng, 20.0f), uniform(rng, 0.2f, 2.0f), 0});
        }
    }
    for (int i = 0; i < 300; ++i) {
        if (i > 0 && rng() % 6u == 0) {
            tris.push_back(tris[rng() % tris.size()]);
        } else {
            const rt::vec3 c = uniform_vec(rng, 20.0f);
            tris.push_back({c + uniform_vec(rng, 2.0f), c + uniform_vec(rng, 2.0f),
                            c + uniform_vec(rng, 2.0f), 0});
        }
    }
    std::vector<rt::Ray> rays;
    for (int k = 0; k < 4096; ++k) {
        const rt::vec3 o = uniform_vec(rng, 40.0f);
        const rt::vec3 aim =
            (k % 2 ? spheres[rng() % spheres.size()].center : tris[rng() % tris.size()].v0) +
            uniform_vec(rng, 1.0f);
        rays.push_back({o, k % 3 == 0 ? 1.0e-3f : 0.0f, aim - o,
                        k % 5 == 0 ? uniform(rng, 0.2f, 1.0f) : INFINITY});
    }
    const rt::Scene host_scene =
        make_scene(spheres.data(), uint32_t(spheres.size()), tris.data(), uint32_t(tris.size()));

    rt::Sphere* d_spheres = to_device(spheres);
    rt::Triangle* d_tris = to_device(tris);
    rt::Ray* d_rays = to_device(rays);
    QueryResult* d_out = nullptr;
    uint32_t* d_flag = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_out, rays.size() * sizeof(QueryResult)));
    RT_CUDA_CHECK(cudaMalloc(&d_flag, sizeof(uint32_t)));
    const rt::Scene dev_scene =
        make_scene(d_spheres, uint32_t(spheres.size()), d_tris, uint32_t(tris.size()));

    for (uint32_t leaf : {1u, 4u}) {
        SCOPED_TRACE("leaf " + std::to_string(leaf));
        rt::BvhParams p;
        p.max_leaf_size = leaf;
        rt::Timings timings;
        const rt::Bvh bvh = rt::build_bvh(host_scene, p, timings);
        ASSERT_TRUE(rt::validate_bvh(bvh, host_scene));
        rt::BvhNode* d_nodes = to_device(bvh.nodes);
        rt::PrimRef* d_refs = to_device(bvh.prim_refs);
        RT_CUDA_CHECK(cudaMemset(d_flag, 0, sizeof(uint32_t)));

        const rt::BvhQuery bq{dev_scene, {d_nodes, d_refs, uint32_t(bvh.nodes.size())}};
        const rt::BruteQuery fq{dev_scene};
        const int n = static_cast<int>(rays.size());
        query_kernel<<<(n + 127) / 128, 128>>>(d_rays, n, bq, fq, d_flag, d_out);
        sync_kernel();
        const std::vector<QueryResult> out = to_host(d_out, rays.size());
        EXPECT_EQ(to_host(d_flag, 1)[0], 0u);
        RT_CUDA_CHECK(cudaFree(d_refs));
        RT_CUDA_CHECK(cudaFree(d_nodes));

        int hits = 0, mismatches = 0;
        for (size_t i = 0; i < out.size(); ++i) {
            const QueryResult& q = out[i];
            const bool same = std::memcmp(&q.bvh_closest, &q.brute_closest, sizeof(rt::Hit)) == 0 &&
                              q.bvh_any == q.brute_any;
            if (!same && ++mismatches <= 5) ADD_FAILURE() << "ray " << i << " differs";
            hits += q.brute_closest.prim != rt::RT_MISS_PRIM;
        }
        EXPECT_EQ(mismatches, 0);
        EXPECT_GT(hits, n / 4);
    }
    RT_CUDA_CHECK(cudaFree(d_flag));
    RT_CUDA_CHECK(cudaFree(d_out));
    RT_CUDA_CHECK(cudaFree(d_rays));
    RT_CUDA_CHECK(cudaFree(d_tris));
    RT_CUDA_CHECK(cudaFree(d_spheres));
}

/// One thread: traverse a too-deep BVH and report through the device flag.
__global__ void overflow_kernel(rt::Ray r, rt::Scene s, rt::BvhView bvh, uint32_t* flag, int* any) {
    rt::traverse_closest(r, s, bvh, flag);
    *any = rt::traverse_any(r, s, bvh, flag);
}

/// A chain one level deeper than RT_MAX_BVH_DEPTH sets the device-global overflow flag
/// (atomicOr) and stops the ray; a chain at the limit does not.
TEST(TraverseDevice, OverflowSetsFlag) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const rt::AABB box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}};
    const std::vector<rt::Sphere> spheres{{{0.0f, 0.0f, 0.0f}, 1.0f, 0}};
    const std::vector<rt::PrimRef> refs{0u};
    rt::Sphere* d_spheres = to_device(spheres);
    rt::PrimRef* d_refs = to_device(refs);
    uint32_t* d_flag = nullptr;
    int* d_any = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_flag, sizeof(uint32_t)));
    RT_CUDA_CHECK(cudaMalloc(&d_any, sizeof(int)));
    const rt::Ray r{{0.0f, 0.0f, -5.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, INFINITY};

    for (int levels : {rt::RT_MAX_BVH_DEPTH, rt::RT_MAX_BVH_DEPTH + 1}) {
        SCOPED_TRACE("levels " + std::to_string(levels));
        // Same chain as test_traverse.cpp: left child interior, right child a leaf.
        std::vector<rt::BvhNode> nodes{{box, 0u, 0u}};
        uint32_t cur = 0;
        for (int k = 0; k < levels; ++k) {
            const uint32_t left = static_cast<uint32_t>(nodes.size());
            nodes[cur].left_or_first = left;
            nodes.push_back({box, 0u, 0u});
            nodes.push_back({box, 0u, 1u});
            cur = left;
        }
        nodes[cur].count = 1u;
        rt::BvhNode* d_nodes = to_device(nodes);
        RT_CUDA_CHECK(cudaMemset(d_flag, 0, sizeof(uint32_t)));
        overflow_kernel<<<1, 1>>>(r, make_scene(d_spheres, 1, nullptr, 0),
                                  {d_nodes, d_refs, uint32_t(nodes.size())}, d_flag, d_any);
        sync_kernel();
        const bool too_deep = levels > rt::RT_MAX_BVH_DEPTH;
        EXPECT_EQ(to_host(d_flag, 1)[0], too_deep ? 1u : 0u);
        EXPECT_EQ(to_host(d_any, 1)[0], too_deep ? 0 : 1);
        RT_CUDA_CHECK(cudaFree(d_nodes));
    }
    RT_CUDA_CHECK(cudaFree(d_any));
    RT_CUDA_CHECK(cudaFree(d_flag));
    RT_CUDA_CHECK(cudaFree(d_refs));
    RT_CUDA_CHECK(cudaFree(d_spheres));
}

}  // namespace

/// @file test_render_pixel_device.cu
/// @brief Runs render_pixel<BruteQuery, NormalShading> in a CUDA kernel, one thread per pixel,
/// and compares the image with the host.
///
/// Built only when RT_ENABLE_CUDA is on, and needs a GPU: a missing device fails the test. This
/// is the correctness half of the GPU smoke image (ROADMAP C3G), without a backend or file IO.
// render_pixel.hpp comes first, so this file also checks that it compiles as CUDA on its own.
#include "shade/render_pixel.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "gpu/cuda_check.hpp"
#include "trace/brute.hpp"

namespace {

// The known_two_spheres scene (scenes/known/known_two_spheres.json), built in code so this CUDA
// file doesn't compile the JSON reader. Materials and lights are unused by NormalShading.
const std::vector<rt::Sphere> kSpheres{{{-0.75f, 0.5f, 0.0f}, 0.5f, 0},
                                       {{0.75f, 0.5f, 0.0f}, 0.5f, 1}};
const std::vector<rt::Triangle> kTriangles{
    {{-5.0f, 0.0f, -5.0f}, {-5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, 5.0f}, 2},
    {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {5.0f, 0.0f, -5.0f}, 2}};
const rt::Camera kCamera{
    {0.0f, 1.0f, 5.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f, 640, 480};
const rt::color kBackground{0.1f, 0.1f, 0.15f};

/// Copies @p v to a new device array. The caller frees it with cudaFree.
template <class T>
T* to_device(const std::vector<T>& v) {
    T* d = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(T)));
    RT_CUDA_CHECK(cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    return d;
}

/// One thread per pixel, as in the megakernel. The scene view and query are passed by value.
__global__ void render_kernel(rt::Camera cam, rt::Scene s, rt::PixelResult* out) {
    const uint32_t px = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= cam.width || py >= cam.height) return;
    out[py * cam.width + px] =
        rt::render_pixel<rt::BruteQuery, rt::NormalShading>(cam, px, py, s, rt::BruteQuery{s});
}

/// True if every pixel in the 3x3 block around (px, py) has the same host prim_id.
bool interior(const std::vector<rt::PixelResult>& img, uint32_t w, uint32_t h, uint32_t px,
              uint32_t py) {
    const uint32_t id = img[py * w + px].prim_id;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = static_cast<int>(px) + dx;
            const int y = static_cast<int>(py) + dy;
            if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h)) continue;
            if (img[y * w + x].prim_id != id) return false;
        }
    }
    return true;
}

/// The device image matches the host: same prim_id away from silhouettes and edges, and the
/// same color within FMA-contraction tolerance wherever prim_id matches.
TEST(RenderPixelDevice, KnownSceneMatchesHost) {
    const uint32_t w = kCamera.width;
    const uint32_t h = kCamera.height;
    rt::Sphere* d_spheres = to_device(kSpheres);
    rt::Triangle* d_triangles = to_device(kTriangles);
    rt::PixelResult* d_out = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_out, size_t{w} * h * sizeof(rt::PixelResult)));

    const rt::Scene d_scene{d_spheres,   static_cast<uint32_t>(kSpheres.size()),
                            d_triangles, static_cast<uint32_t>(kTriangles.size()),
                            nullptr,     0,
                            nullptr,     0,
                            kBackground, 0.05f};
    const dim3 block(16, 16);
    const dim3 grid((w + block.x - 1) / block.x, (h + block.y - 1) / block.y);
    render_kernel<<<grid, block>>>(kCamera, d_scene, d_out);
    RT_CUDA_CHECK(cudaGetLastError());
    RT_CUDA_CHECK(cudaDeviceSynchronize());
    std::vector<rt::PixelResult> device(size_t{w} * h);
    RT_CUDA_CHECK(cudaMemcpy(device.data(), d_out, device.size() * sizeof(rt::PixelResult),
                             cudaMemcpyDeviceToHost));
    RT_CUDA_CHECK(cudaFree(d_out));
    RT_CUDA_CHECK(cudaFree(d_triangles));
    RT_CUDA_CHECK(cudaFree(d_spheres));

    const rt::Scene h_scene{kSpheres.data(),   static_cast<uint32_t>(kSpheres.size()),
                            kTriangles.data(), static_cast<uint32_t>(kTriangles.size()),
                            nullptr,           0,
                            nullptr,           0,
                            kBackground,       0.05f};
    std::vector<rt::PixelResult> host(device.size());
    for (uint32_t py = 0; py < h; ++py)
        for (uint32_t px = 0; px < w; ++px)
            host[py * w + px] = rt::render_pixel(kCamera, px, py, h_scene, rt::BruteQuery{h_scene},
                                                 rt::NormalShading{});

    // nvcc contracts a*b + c into FMA by default and GCC may not, so camera rays and sphere
    // hits can differ by an ulp. That can flip prim_id only on a silhouette or an edge; inside
    // an object the hit must agree, and the color within a small tolerance.
    uint32_t edge_mismatches = 0;
    uint32_t hits = 0;
    for (uint32_t py = 0; py < h; ++py) {
        for (uint32_t px = 0; px < w; ++px) {
            const rt::PixelResult& d = device[py * w + px];
            const rt::PixelResult& c = host[py * w + px];
            ASSERT_TRUE(std::isfinite(d.rgb.x) && std::isfinite(d.rgb.y) && std::isfinite(d.rgb.z))
                << "nonfinite device pixel " << px << "," << py;
            EXPECT_EQ(d.light_mask, 0u);
            if (d.prim_id != c.prim_id) {
                ASSERT_FALSE(interior(host, w, h, px, py))
                    << "prim_id differs inside an object at " << px << "," << py;
                ++edge_mismatches;
                continue;
            }
            hits += d.prim_id != rt::RT_MISS_PRIM;
            EXPECT_NEAR(d.rgb.x, c.rgb.x, 1.0e-4f) << px << "," << py;
            EXPECT_NEAR(d.rgb.y, c.rgb.y, 1.0e-4f) << px << "," << py;
            EXPECT_NEAR(d.rgb.z, c.rgb.z, 1.0e-4f) << px << "," << py;
        }
    }
    EXPECT_LT(edge_mismatches, w * h / 1000) << "more edge flips than FMA differences explain";
    EXPECT_GT(hits, w * h / 2);
    RecordProperty("edge_mismatches", static_cast<int>(edge_mismatches));
}

}  // namespace

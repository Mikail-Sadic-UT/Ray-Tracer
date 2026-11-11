/// @file test_shade_device.cu
/// @brief Runs render_pixel with LitShading in a CUDA kernel and compares the lit
/// image of known_two_spheres with the host.
///
/// Built only when RT_ENABLE_CUDA is on. Skips when no CUDA device is present; a CUDA error on a
/// machine that has a GPU fails the test.
// render_pixel.hpp comes first, so this file also checks that it compiles as CUDA on its own.
#include "shade/render_pixel.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "gpu/cuda_check.hpp"
#include "known_two_spheres_data.hpp"
#include "trace/brute.hpp"

namespace {

using known_two_spheres::kAmbient;
using known_two_spheres::kBackground;
using known_two_spheres::kCamera;
using known_two_spheres::kSpheres;
using known_two_spheres::kTriangles;

// Materials and the light of scenes/known/known_two_spheres.json.
const std::vector<rt::Material> kMaterials{{{0.8f, 0.1f, 0.1f}, {}, 1.0f, 0u},
                                           {{0.1f, 0.1f, 0.8f}, {}, 1.0f, 0u},
                                           {{0.7f, 0.7f, 0.7f}, {}, 1.0f, 0u}};
const std::vector<rt::Light> kLights{{rt::LIGHT_POINT, {2.0f, 4.0f, 2.0f}, {20.0f, 20.0f, 20.0f}}};

// rt_compare's starting tolerances (BENCHMARKING.md, "Image comparison"): per-channel
// |a - b| <= tau_abs + tau_rel * |ref|, and at most 0.05% of pixels may flip prim_id or light_mask.
// The final values are set at G2 from real CPU/GPU data, and this test follows them.
constexpr float kTauAbs = 1e-5f;
constexpr float kTauRel = 1e-4f;
constexpr double kBudgetPx = 0.0005;

/// Copies @p v to a new device array. The caller frees it with cudaFree.
template <class T>
T* to_device(const std::vector<T>& v) {
    T* d = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(T)));
    RT_CUDA_CHECK(cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    return d;
}

/// One thread per pixel, shading with LitShading.
__global__ void lit_kernel(rt::Camera cam, rt::Scene s, rt::PixelResult* out) {
    const uint32_t px = blockIdx.x * blockDim.x + threadIdx.x;
    const uint32_t py = blockIdx.y * blockDim.y + threadIdx.y;
    if (px >= cam.width || py >= cam.height) return;
    out[py * cam.width + px] =
        rt::render_pixel<rt::BruteQuery, rt::LitShading>(cam, px, py, s, rt::BruteQuery{s});
}

/// The lit device image matches the host: finite, same prim_id and light_mask except at a bounded
/// number of silhouette and shadow-edge pixels, and the same color within FMA tolerance.
TEST(ShadeDevice, LitKnownSceneMatchesHost) {
    int devices = 0;
    if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0)
        GTEST_SKIP() << "no CUDA device";

    const uint32_t w = kCamera.width;
    const uint32_t h = kCamera.height;
    rt::Sphere* d_spheres = to_device(kSpheres);
    rt::Triangle* d_triangles = to_device(kTriangles);
    rt::Material* d_materials = to_device(kMaterials);
    rt::Light* d_lights = to_device(kLights);
    rt::PixelResult* d_out = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_out, size_t{w} * h * sizeof(rt::PixelResult)));

    const rt::Scene d_scene{d_spheres,   static_cast<uint32_t>(kSpheres.size()),
                            d_triangles, static_cast<uint32_t>(kTriangles.size()),
                            d_materials, static_cast<uint32_t>(kMaterials.size()),
                            d_lights,    static_cast<uint32_t>(kLights.size()),
                            kBackground, kAmbient};
    const dim3 block(16, 16);
    const dim3 grid((w + block.x - 1) / block.x, (h + block.y - 1) / block.y);
    lit_kernel<<<grid, block>>>(kCamera, d_scene, d_out);
    RT_CUDA_CHECK(cudaGetLastError());
    RT_CUDA_CHECK(cudaDeviceSynchronize());
    std::vector<rt::PixelResult> device(size_t{w} * h);
    RT_CUDA_CHECK(cudaMemcpy(device.data(), d_out, device.size() * sizeof(rt::PixelResult),
                             cudaMemcpyDeviceToHost));
    RT_CUDA_CHECK(cudaFree(d_out));
    RT_CUDA_CHECK(cudaFree(d_lights));
    RT_CUDA_CHECK(cudaFree(d_materials));
    RT_CUDA_CHECK(cudaFree(d_triangles));
    RT_CUDA_CHECK(cudaFree(d_spheres));

    const rt::Scene h_scene{kSpheres.data(),   static_cast<uint32_t>(kSpheres.size()),
                            kTriangles.data(), static_cast<uint32_t>(kTriangles.size()),
                            kMaterials.data(), static_cast<uint32_t>(kMaterials.size()),
                            kLights.data(),    static_cast<uint32_t>(kLights.size()),
                            kBackground,       kAmbient};
    uint32_t id_flips = 0;
    uint32_t mask_flips = 0;
    uint32_t lit = 0;
    for (uint32_t py = 0; py < h; ++py) {
        for (uint32_t px = 0; px < w; ++px) {
            const rt::PixelResult c = rt::render_pixel<rt::BruteQuery, rt::LitShading>(
                kCamera, px, py, h_scene, rt::BruteQuery{h_scene});
            const rt::PixelResult& d = device[py * w + px];
            ASSERT_TRUE(std::isfinite(d.rgb.x) && std::isfinite(d.rgb.y) && std::isfinite(d.rgb.z))
                << "nonfinite device pixel " << px << "," << py;
            if (d.prim_id != c.prim_id) {
                ++id_flips;
                continue;
            }
            if (d.light_mask != c.light_mask) {
                ++mask_flips;
                continue;
            }
            lit += d.light_mask != 0u;
            EXPECT_NEAR(d.rgb.x, c.rgb.x, kTauAbs + kTauRel * std::fabs(c.rgb.x))
                << px << "," << py;
            EXPECT_NEAR(d.rgb.y, c.rgb.y, kTauAbs + kTauRel * std::fabs(c.rgb.y))
                << px << "," << py;
            EXPECT_NEAR(d.rgb.z, c.rgb.z, kTauAbs + kTauRel * std::fabs(c.rgb.z))
                << px << "," << py;
        }
    }
    EXPECT_LE(id_flips + mask_flips, w * h * kBudgetPx)
        << "more edge flips than FMA differences explain";
    EXPECT_GT(lit, w * h / 4);
    RecordProperty("id_flips", static_cast<int>(id_flips));
    RecordProperty("mask_flips", static_cast<int>(mask_flips));
}

}  // namespace

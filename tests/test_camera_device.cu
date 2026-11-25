/// @file test_camera_device.cu
/// @brief camera_ray and tan_det give the same bits in a CUDA kernel as on the host.
///
/// `tanf` did not: glibc and CUDA differ in the last bit for about a third of all angles (70
/// degrees among them), which moved every camera ray and broke byte-equal CPU and GPU images for
/// any field of view but the few that happen to agree. Built only with RT_ENABLE_CUDA; skipped
/// when no CUDA device is visible.
#include "camera/camera.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cuda_test_util.hpp"
#include "gpu/cuda_check.hpp"

namespace {

__global__ void tan_kernel(const float* x, float* out, int n) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = rt::tan_det(x[i]);
}

__global__ void ray_kernel(rt::Camera base, const float* vfov, rt::Ray* out, int n) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    rt::Camera cam = base;
    cam.vfov_deg = vfov[i];
    // Three pixels per field of view: a corner, the middle and the opposite corner.
    out[3 * i + 0] = rt::camera_ray(cam, 0, 0);
    out[3 * i + 1] = rt::camera_ray(cam, cam.width / 2, cam.height / 2);
    out[3 * i + 2] = rt::camera_ray(cam, cam.width - 1, cam.height - 1);
}

template <class T>
T* to_device(const std::vector<T>& v) {
    T* d = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(T)));
    RT_CUDA_CHECK(cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    return d;
}

/// Every field of view from 1 to 179 degrees in steps of 0.37.
std::vector<float> field_of_views() {
    std::vector<float> v;
    for (float f = 1.0f; f < 179.0f; f += 0.37f) v.push_back(f);
    v.push_back(70.0f);  // the angle that first showed the problem
    return v;
}

}  // namespace

TEST(CameraDevice, TanDetMatchesTheHostBitForBit) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const float pi = 3.14159265358979323846f;
    std::vector<float> x;
    for (float f : field_of_views()) x.push_back(f * pi / 360.0f);
    const int n = static_cast<int>(x.size());
    float* d_x = to_device(x);
    float* d_out = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_out, n * sizeof(float)));
    tan_kernel<<<(n + 127) / 128, 128>>>(d_x, d_out, n);
    RT_CUDA_CHECK(cudaDeviceSynchronize());
    std::vector<float> device(n);
    RT_CUDA_CHECK(cudaMemcpy(device.data(), d_out, n * sizeof(float), cudaMemcpyDeviceToHost));
    RT_CUDA_CHECK(cudaFree(d_x));
    RT_CUDA_CHECK(cudaFree(d_out));
    for (int i = 0; i < n; ++i) {
        const float host = rt::tan_det(x[i]);
        EXPECT_EQ(std::memcmp(&host, &device[i], sizeof(float)), 0) << "x " << x[i];
    }
}

TEST(CameraDevice, CameraRaysMatchTheHostBitForBit) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::Camera base;
    base.position = {0.6f, 1.7f, 3.6f};
    base.look_at = {0.0f, 1.0f, -2.0f};
    base.up = {0.0f, 1.0f, 0.0f};
    base.vfov_deg = 45.0f;
    base.width = 1280;
    base.height = 720;
    const std::vector<float> vfov = field_of_views();
    const int n = static_cast<int>(vfov.size());
    float* d_vfov = to_device(vfov);
    rt::Ray* d_out = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_out, 3 * n * sizeof(rt::Ray)));
    ray_kernel<<<(n + 127) / 128, 128>>>(base, d_vfov, d_out, n);
    RT_CUDA_CHECK(cudaDeviceSynchronize());
    std::vector<rt::Ray> device(3 * n);
    RT_CUDA_CHECK(
        cudaMemcpy(device.data(), d_out, 3 * n * sizeof(rt::Ray), cudaMemcpyDeviceToHost));
    RT_CUDA_CHECK(cudaFree(d_vfov));
    RT_CUDA_CHECK(cudaFree(d_out));
    for (int i = 0; i < n; ++i) {
        rt::Camera cam = base;
        cam.vfov_deg = vfov[i];
        const rt::Ray host[3] = {rt::camera_ray(cam, 0, 0),
                                 rt::camera_ray(cam, cam.width / 2, cam.height / 2),
                                 rt::camera_ray(cam, cam.width - 1, cam.height - 1)};
        for (int k = 0; k < 3; ++k) {
            EXPECT_EQ(std::memcmp(&host[k], &device[3 * i + k], sizeof(rt::Ray)), 0)
                << "vfov " << vfov[i] << " pixel " << k;
        }
    }
}

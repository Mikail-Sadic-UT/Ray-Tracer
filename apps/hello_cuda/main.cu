/// @file main.cu
/// @brief Prints each visible GPU's name, SM count, and compute capability, then runs a tiny
/// kernel that calls shared RT_HOSTDEV code to prove the device path works.
#include <cuda_runtime.h>

#include <cstdio>
#include <exception>

#include "core/error.hpp"
#include "core/math.hpp"
#include "gpu/cuda_check.hpp"

namespace {

/// Writes dot(a, b) using the shared math library, compiled for the device.
__global__ void dot_kernel(rt::vec3 a, rt::vec3 b, float* out) {
    *out = rt::dot(a, b);
}

}  // namespace

int main() {
    try {
        int runtime = 0;
        int driver = 0;
        RT_CUDA_CHECK(cudaRuntimeGetVersion(&runtime));
        RT_CUDA_CHECK(cudaDriverGetVersion(&driver));
        std::printf("CUDA runtime %d.%d, driver supports %d.%d\n", runtime / 1000,
                    (runtime % 1000) / 10, driver / 1000, (driver % 1000) / 10);

        int count = 0;
        RT_CUDA_CHECK(cudaGetDeviceCount(&count));
        for (int i = 0; i < count; ++i) {
            cudaDeviceProp prop{};
            RT_CUDA_CHECK(cudaGetDeviceProperties(&prop, i));
            std::printf("GPU %d: %s, %d SMs, compute capability %d.%d\n", i, prop.name,
                        prop.multiProcessorCount, prop.major, prop.minor);
        }

        float* d_out = nullptr;
        RT_CUDA_CHECK(cudaMalloc(&d_out, sizeof(float)));
        dot_kernel<<<1, 1>>>(rt::vec3{1.0f, 2.0f, 3.0f}, rt::vec3{4.0f, 5.0f, 6.0f}, d_out);
        RT_CUDA_CHECK(cudaGetLastError());
        float result = 0.0f;
        RT_CUDA_CHECK(cudaMemcpy(&result, d_out, sizeof(float), cudaMemcpyDeviceToHost));
        RT_CUDA_CHECK(cudaFree(d_out));
        if (result != 32.0f) {
            throw rt::Error("hello_cuda: device dot() returned " + std::to_string(result) +
                            ", expected 32");
        }
        std::printf("Device kernel OK: dot((1,2,3), (4,5,6)) = %g\n", result);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}

/// @file cuda_check.hpp
/// @brief RT_CUDA_CHECK: turns a failed CUDA runtime call into rt::Error.
///
/// Host-only. Include only from .cu files or host code that links CUDA::cudart.
#pragma once

#include <cuda_runtime.h>

#include <string>

#include "core/error.hpp"

namespace rt::detail {

/// Throws rt::Error naming the failed call, its location, and the CUDA error string.
[[noreturn]] inline void cuda_fail(cudaError_t err, const char* expr, const char* file, int line) {
    throw Error(std::string(file) + ":" + std::to_string(line) + ": " + expr +
                " failed: " + cudaGetErrorString(err));
}

}  // namespace rt::detail

/// @def RT_CUDA_CHECK
/// Evaluates a CUDA runtime call and throws rt::Error if it does not return cudaSuccess.
#define RT_CUDA_CHECK(expr)                                                   \
    do {                                                                      \
        const cudaError_t rt_cuda_err_ = (expr);                              \
        if (rt_cuda_err_ != cudaSuccess) {                                    \
            ::rt::detail::cuda_fail(rt_cuda_err_, #expr, __FILE__, __LINE__); \
        }                                                                     \
    } while (0)

namespace rt {

/// True if at least one CUDA device is visible.
///
/// Returns false only when the runtime reports `cudaErrorNoDevice` (no GPU, or
/// `CUDA_VISIBLE_DEVICES` is empty). Any other failure, such as a driver too old for the
/// toolkit, throws rt::Error, so a broken setup on a machine with a GPU is not mistaken for a
/// missing GPU.
inline bool cuda_device_available() {
    int devices = 0;
    const cudaError_t err = cudaGetDeviceCount(&devices);
    if (err == cudaErrorNoDevice) {
        (void)cudaGetLastError();  // clear the error so later runtime calls are unaffected
        return false;
    }
    RT_CUDA_CHECK(err);
    return devices > 0;
}

}  // namespace rt

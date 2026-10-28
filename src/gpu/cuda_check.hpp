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

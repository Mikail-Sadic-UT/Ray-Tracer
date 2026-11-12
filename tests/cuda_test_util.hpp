/// @file cuda_test_util.hpp
/// @brief Helpers for GoogleTest cases that run CUDA kernels.
#pragma once

#include <gtest/gtest.h>

#include "gpu/cuda_check.hpp"

/// Skips the current test when no CUDA device is visible, so machines with the toolkit but no
/// GPU (WORKFLOW.md "Development environment") see skipped tests, not failures. Other CUDA
/// errors still fail the test. Use it as the first statement of every test that launches a
/// kernel or allocates device memory.
#define RT_SKIP_WITHOUT_CUDA_DEVICE()                             \
    do {                                                          \
        if (!::rt::cuda_device_available()) {                     \
            GTEST_SKIP() << "no CUDA device (cudaErrorNoDevice)"; \
        }                                                         \
    } while (0)

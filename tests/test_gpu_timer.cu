/// @file test_gpu_timer.cu
/// @brief Tests for the CUDA timing helpers. Tests that launch kernels skip without a device.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "bench/gpu_timer.hpp"
#include "core/error.hpp"
#include "cuda_test_util.hpp"

namespace {

/// Spins one thread for about @p ns nanoseconds of GPU time.
__global__ void spin_kernel(std::uint64_t ns) {
    std::uint64_t begin;
    asm volatile("mov.u64 %0, %%globaltimer;" : "=l"(begin));
    std::uint64_t now = begin;
    while (now - begin < ns) {
        asm volatile("mov.u64 %0, %%globaltimer;" : "=l"(now));
    }
}

}  // namespace

/// Checks that, with no device, initialization reports rt::Error instead of crashing. Runs only
/// where there is no GPU.
TEST(GpuTimer, InitWithoutDeviceThrowsRtError) {
    // Same rule as RT_SKIP_WITHOUT_CUDA_DEVICE: a driver that is too old throws here and fails.
    if (rt::cuda_device_available()) GTEST_SKIP() << "a CUDA device is present";
    EXPECT_THROW(rt::cuda_init_ms(), rt::Error);
    rt::MachineInfo info;
    EXPECT_THROW(rt::fill_gpu_machine_info(info), rt::Error);
    EXPECT_TRUE(info.gpu_name.empty());
}

/// Checks the event timer's call-order rules.
TEST(GpuTimer, EventTimerRejectsOutOfOrderCalls) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::CudaEventTimer timer;
    EXPECT_THROW(timer.stop(), rt::Error);
    EXPECT_THROW(timer.elapsed_ms(), rt::Error);
    timer.start();
    EXPECT_THROW(timer.elapsed_ms(), rt::Error);
}

/// Checks that cuda_init_ms() reports a finite non-negative time and a second call is not slower
/// than the first by an order of magnitude (the context already exists).
TEST(GpuTimer, InitReportsContextCreationTime) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const double first = rt::cuda_init_ms();
    const double second = rt::cuda_init_ms();
    EXPECT_GE(first, 0.0);
    EXPECT_GE(second, 0.0);
    EXPECT_LE(second, first + 50.0);
}

/// Checks that the event timer measures a kernel of known duration and agrees with the host.
TEST(GpuTimer, EventTimerMeasuresKernelTime) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::cuda_init_ms();
    spin_kernel<<<1, 1>>>(1000);  // warm up the launch path
    RT_CUDA_CHECK(cudaDeviceSynchronize());

    rt::CudaEventTimer timer;
    rt::WallTimer wall;
    timer.start();
    spin_kernel<<<1, 1>>>(20'000'000);  // 20 ms
    timer.stop();
    const double kernel_ms = timer.elapsed_ms();
    const double wall_ms = wall.elapsed_ms();
    EXPECT_GE(kernel_ms, 19.0);
    EXPECT_LE(kernel_ms, wall_ms + 0.5);  // events lie inside the host interval
}

/// Checks that CudaStageTimer includes asynchronous kernel work that was launched inside it and
/// excludes work launched before it.
TEST(GpuTimer, StageTimerSynchronizesAtBothBoundaries) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::cuda_init_ms();
    spin_kernel<<<1, 1>>>(60'000'000);  // 60 ms of earlier work, still running
    double slot = -1.0;
    {
        rt::CudaStageTimer stage(slot);  // waits for the earlier kernel
        spin_kernel<<<1, 1>>>(20'000'000);
        stage.stop();
    }
    EXPECT_GE(slot, 19.0);
    EXPECT_LT(slot, 50.0);  // not the earlier 60 ms
}

/// Checks the GPU identity fields.
TEST(GpuTimer, FillsGpuMachineInfo) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::MachineInfo info;
    rt::fill_gpu_machine_info(info);
    EXPECT_FALSE(info.gpu_name.empty());
    EXPECT_EQ(info.gpu_uuid.rfind("GPU-", 0), 0u) << info.gpu_uuid;
    EXPECT_EQ(info.gpu_uuid.size(), 4u + 36u) << info.gpu_uuid;
    EXPECT_NE(info.cuda_runtime.find('.'), std::string::npos) << info.cuda_runtime;
    EXPECT_FALSE(info.driver.empty());
}

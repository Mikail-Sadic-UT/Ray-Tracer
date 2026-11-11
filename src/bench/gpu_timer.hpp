/// @file gpu_timer.hpp
/// @brief CUDA timing helpers: event timer for kernel-only time, synchronized stage timer,
///        context-initialization timer, and GPU identity (INTERFACES section 8).
///
/// Host-only and header-only. Include it from `.cu` files or host code that links `CUDA::cudart`;
/// `rt_bench` itself never includes it, so the CPU-only build stays CUDA-free. Every function
/// throws rt::Error through RT_CUDA_CHECK when the CUDA runtime reports a failure, including
/// `cudaErrorNoDevice` on a machine without a GPU.
#pragma once

#include <cuda_runtime.h>

#include <cstdio>
#include <string>

#include "bench/machine_info.hpp"
#include "bench/timer.hpp"
#include "gpu/cuda_check.hpp"

namespace rt {

/// Kernel-only time between two points of a CUDA stream, from `cudaEvent`s.
///
/// This is the `render_kernel` diagnostic: it overlaps the host `render` interval and is never
/// part of a component sum.
class CudaEventTimer {
   public:
    /// Creates the two events.
    CudaEventTimer() {
        RT_CUDA_CHECK(cudaEventCreate(&begin_));
        if (const cudaError_t err = cudaEventCreate(&end_); err != cudaSuccess) {
            cudaEventDestroy(begin_);
            detail::cuda_fail(err, "cudaEventCreate(&end_)", __FILE__, __LINE__);
        }
    }

    CudaEventTimer(const CudaEventTimer&) = delete;
    CudaEventTimer& operator=(const CudaEventTimer&) = delete;

    /// Destroys the events.
    ~CudaEventTimer() {
        cudaEventDestroy(begin_);
        cudaEventDestroy(end_);
    }

    /// Records the start event on @p stream (default stream if null).
    void start(cudaStream_t stream = nullptr) {
        RT_CUDA_CHECK(cudaEventRecord(begin_, stream));
        started_ = true;
        stopped_ = false;
    }

    /// Records the end event on @p stream, which must be the stream passed to start().
    /// @throws Error if start() was not called.
    void stop(cudaStream_t stream = nullptr) {
        if (!started_) throw Error("CudaEventTimer: stop() before start()");
        RT_CUDA_CHECK(cudaEventRecord(end_, stream));
        stopped_ = true;
    }

    /// Waits for the end event and returns the milliseconds between the two events.
    /// @throws Error if start() and stop() were not both called.
    double elapsed_ms() {
        if (!started_ || !stopped_) throw Error("CudaEventTimer: elapsed_ms() before stop()");
        RT_CUDA_CHECK(cudaEventSynchronize(end_));
        float ms = 0.0f;
        RT_CUDA_CHECK(cudaEventElapsedTime(&ms, begin_, end_));
        return static_cast<double>(ms);
    }

   private:
    cudaEvent_t begin_ = nullptr;
    cudaEvent_t end_ = nullptr;
    bool started_ = false;
    bool stopped_ = false;
};

/// Host wall-clock stage timer with `cudaDeviceSynchronize` at both boundaries.
///
/// The constructor synchronizes, so earlier asynchronous work is not counted, and starts the
/// clock. stop() synchronizes, then assigns the elapsed milliseconds to the slot. Call stop()
/// explicitly so a CUDA failure reaches the caller; the destructor stops a forgotten timer but
/// swallows its error.
class CudaStageTimer {
   public:
    /// Synchronizes the device, then starts timing into @p slot_ms, which must outlive this.
    explicit CudaStageTimer(double& slot_ms) : slot_(slot_ms) {
        RT_CUDA_CHECK(cudaDeviceSynchronize());
        timer_.restart();
    }

    CudaStageTimer(const CudaStageTimer&) = delete;
    CudaStageTimer& operator=(const CudaStageTimer&) = delete;

    /// Stops a timer that stop() did not.
    ~CudaStageTimer() {
        try {
            stop();
        } catch (...) {
        }
    }

    /// Synchronizes the device and assigns the elapsed time to the slot. Later calls do nothing.
    void stop() {
        if (stopped_) return;
        stopped_ = true;
        RT_CUDA_CHECK(cudaDeviceSynchronize());
        slot_ = timer_.elapsed_ms();
    }

   private:
    double& slot_;
    WallTimer timer_;
    bool stopped_ = false;
};

/// Creates the CUDA context and returns the milliseconds it took (`Timings::cuda_init`).
///
/// Run it once, before any other CUDA call, so the cost lands in `cuda_init` and not in the
/// first `alloc` or kernel launch.
/// @throws Error if the runtime cannot initialize, for example with no device.
inline double cuda_init_ms() {
    WallTimer timer;
    RT_CUDA_CHECK(cudaFree(nullptr));
    RT_CUDA_CHECK(cudaDeviceSynchronize());
    return timer.elapsed_ms();
}

namespace detail {

/// `major.minor` from a CUDA version integer such as 13030.
inline std::string cuda_version_string(int version) {
    return std::to_string(version / 1000) + "." + std::to_string(version % 1000 / 10);
}

/// First line of `nvidia-smi --query-gpu=driver_version`, or empty if it is unavailable.
inline std::string nvidia_smi_driver() {
    FILE* pipe =
        popen("nvidia-smi --query-gpu=driver_version --format=csv,noheader 2>/dev/null", "r");
    if (pipe == nullptr) return {};
    char line[128] = {};
    const bool read = std::fgets(line, sizeof line, pipe) != nullptr;
    pclose(pipe);
    std::string text = read ? line : "";
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

}  // namespace detail

/// Fills the GPU and CUDA fields of @p info from device @p device.
///
/// `driver` is the NVIDIA driver version from `nvidia-smi` (the value BENCHMARKING "Machine
/// identity" asks for); if `nvidia-smi` is unavailable it is the CUDA driver API version.
/// @throws Error if there is no such device or the runtime fails.
inline void fill_gpu_machine_info(MachineInfo& info, int device = 0) {
    cudaDeviceProp props{};
    RT_CUDA_CHECK(cudaGetDeviceProperties(&props, device));
    info.gpu_name = props.name;

    // cudaUUID_t holds 16 bytes; nvidia-smi prints them as GPU-8-4-4-4-12 hex digits.
    std::string uuid = "GPU-";
    char byte[3];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(byte, sizeof byte, "%02x", static_cast<unsigned char>(props.uuid.bytes[i]));
        uuid += byte;
        if (i == 3 || i == 5 || i == 7 || i == 9) uuid += '-';
    }
    info.gpu_uuid = uuid;

    int runtime = 0;
    int driver = 0;
    RT_CUDA_CHECK(cudaRuntimeGetVersion(&runtime));
    RT_CUDA_CHECK(cudaDriverGetVersion(&driver));
    info.cuda_runtime = detail::cuda_version_string(runtime);
    info.driver = detail::nvidia_smi_driver();
    if (info.driver.empty()) info.driver = detail::cuda_version_string(driver);
}

}  // namespace rt

/// @file machine_info.hpp
/// @brief Identity of the machine and the build that produced a measurement (INTERFACES
///        section 8 `machine` and `build`).
#pragma once

#include <string>

namespace rt {

/// Host description recorded with every results file. Unknown strings are empty.
///
/// The CPU-only library fills everything except the GPU fields; a CUDA target fills those with
/// `fill_gpu_machine_info()` (bench/gpu_timer.hpp).
struct MachineInfo {
    std::string host;       ///< Host name.
    std::string cpu_model;  ///< CPU model name (`lscpu` model on Linux, the brand string on macOS).
    int cpu_cores = 0;      ///< Logical processors online (hardware threads).
    std::string gpu_name;   ///< `cudaGetDeviceProperties` name; empty without a GPU.
    std::string gpu_uuid;   ///< GPU UUID, `GPU-xxxxxxxx-...`; empty without a GPU.
    std::string driver;     ///< CUDA driver version, `major.minor`; empty without a GPU.
    std::string cuda_runtime;  ///< CUDA runtime version, `major.minor`; empty without CUDA.
    std::string nvcc;          ///< nvcc version the build was configured with; empty if none.
    std::string gcc;           ///< Host compiler (GCC or Clang) version; the schema key is `gcc`.
    std::string os;            ///< Operating system name and version.
};

/// What source tree and options produced the binary.
struct BuildInfo {
    std::string commit;       ///< `git rev-parse HEAD` at build time, or "unknown".
    bool dirty = false;       ///< Tracked files differed from `commit` at build time.
    std::string cmake_flags;  ///< Build type, CUDA and arch options, and compiler.
};

/// Describes this machine. Linux reads `/proc`, `/etc/os-release` and `lscpu`; macOS reads
/// `sysctl`; elsewhere the model and OS fall back to `uname`. GPU fields stay empty.
MachineInfo collect_machine_info();

/// Returns the identity the build recorded. Refreshed on every build, not only on configure.
BuildInfo collect_build_info();

}  // namespace rt

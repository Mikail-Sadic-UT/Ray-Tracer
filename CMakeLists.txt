# 3.28: oldest version tested (Ubuntu 24.04 apt, CI). FetchContent_Declare(... SYSTEM) fails to
# configure on 3.24 and 3.25.0. Vista provides 3.29.5, 3.31.5, and 4.1.1.
cmake_minimum_required(VERSION 3.28)
project(HybridRayTracer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RT_ENABLE_CUDA   "Build CUDA backend"            ON)
option(RT_ENABLE_EMBREE "Build Embree backend (ext)"    OFF)
option(RT_ENABLE_OPTIX  "Build OptiX backend (stretch)" OFF)
option(RT_BUILD_TESTS   "Build tests"                   ON)
set(RT_CUDA_ARCH "89" CACHE STRING "CUDA arch: 89 laptop, 90 Vista Hopper, 80 A100")
set(RT_CUDA_PINNED "13.3" CACHE STRING "CUDA major.minor the toolchain is pinned to (BUILD.md)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RtDependencies)

find_package(OpenMP REQUIRED)

# CUDA is optional: the CPU-only build must always work.
if(RT_ENABLE_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    if(NOT DEFINED CMAKE_CUDA_HOST_COMPILER)
      set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}")  # nvcc uses the same GCC as C++
    endif()
    set(CMAKE_CUDA_ARCHITECTURES ${RT_CUDA_ARCH})
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    find_package(CUDAToolkit REQUIRED)
    # Warn, don't block: other CUDA releases may work, but results are verified on the pin.
    if(NOT CMAKE_CUDA_COMPILER_VERSION MATCHES "^${RT_CUDA_PINNED}\\.")
      message(WARNING "CUDA ${CMAKE_CUDA_COMPILER_VERSION} differs from the pinned "
                      "${RT_CUDA_PINNED} (BUILD.md). Laptop: cuda-toolkit-13-3. Vista: "
                      "module load cuda/13.3. Note it in the commit (WORKFLOW.md "
                      "\"Development environment\").")
    endif()
  else()
    message(WARNING "No CUDA compiler found, building CPU-only")
    set(RT_ENABLE_CUDA OFF)
  endif()
endif()

include(RtCompilerFlags)  # after CUDA, so its fast-math guard sees the CUDA flags

if(RT_ENABLE_EMBREE)
  message(FATAL_ERROR "RT_ENABLE_EMBREE: the Embree backend (E4) is not implemented yet")
endif()
if(RT_ENABLE_OPTIX)
  message(FATAL_ERROR "RT_ENABLE_OPTIX: the OptiX backend (stretch) is not implemented yet")
endif()

add_subdirectory(src)
add_subdirectory(apps)
if(RT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "RT: build type ${CMAKE_BUILD_TYPE}, CUDA ${RT_ENABLE_CUDA}"
               " (arch ${RT_CUDA_ARCH}), tests ${RT_BUILD_TESTS}")

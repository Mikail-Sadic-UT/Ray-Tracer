# rt_bench: timers, run statistics, the run harness, machine identity, and the results writer
# (INTERFACES section 0 "CMake targets"). Host-only and CUDA-free; gpu_timer.hpp is a header that
# only CUDA targets include.
add_library(rt_bench STATIC stats.cpp harness.cpp machine_info.cpp)
target_link_libraries(rt_bench PUBLIC rt_core nlohmann_json::nlohmann_json)

# The git commit, dirty flag and options are read on every build (not only on configure), so a
# results file never names a commit the binary was not built from.
set(rt_bench_flags "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} RT_ENABLE_CUDA=${RT_ENABLE_CUDA}")
if(RT_ENABLE_CUDA)
  string(APPEND rt_bench_flags " RT_CUDA_ARCH=${RT_CUDA_ARCH}")
endif()
string(APPEND rt_bench_flags
       " RT_ENABLE_EMBREE=${RT_ENABLE_EMBREE} RT_ENABLE_OPTIX=${RT_ENABLE_OPTIX}"
       " CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
set(rt_bench_nvcc "")
if(RT_ENABLE_CUDA AND CMAKE_CUDA_COMPILER_VERSION)
  set(rt_bench_nvcc "${CMAKE_CUDA_COMPILER_VERSION}")
endif()
set(rt_build_info_header ${CMAKE_CURRENT_BINARY_DIR}/rt_build_info.hpp)
add_custom_target(rt_build_info
  COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${rt_build_info_header}
          "-DFLAGS=${rt_bench_flags}" -DNVCC=${rt_bench_nvcc}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/BuildInfo.cmake
  BYPRODUCTS ${rt_build_info_header}
  COMMENT "Recording the git commit and build options"
  VERBATIM)
add_dependencies(rt_bench rt_build_info)
target_include_directories(rt_bench PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

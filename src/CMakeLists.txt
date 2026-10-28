# rt_core: the header-only shared core (INTERFACES section 0 "CMake targets").
# It exports the include root src/, so every target includes headers as "core/math.hpp".
add_library(rt_core INTERFACE)
target_include_directories(rt_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rt_core INTERFACE cxx_std_17)
target_compile_options(rt_core INTERFACE
  $<$<COMPILE_LANGUAGE:CXX>:${RT_CXX_FLAGS}>
  $<$<COMPILE_LANGUAGE:CUDA>:${RT_CUDA_FLAGS}>)

# Each module owner adds a CMakeLists.txt to their directory that defines only their target
# (rt_scene, rt_bvh, ...). Directories without one are skipped.
foreach(module scene camera image bvh bench cpu gpu embree optix)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${module}/CMakeLists.txt)
    add_subdirectory(${module})
  endif()
endforeach()

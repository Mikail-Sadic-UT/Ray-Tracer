# rt_gpu: DeviceScene and the CUDA megakernel backend (INTERFACES section 0 "CMake targets").
# Built only with CUDA; the CPU-only build skips it, and rt_render checks if(TARGET rt_gpu).
if(NOT RT_ENABLE_CUDA)
  return()
endif()
add_library(rt_gpu STATIC device_scene.cu cuda_backend.cu)
target_link_libraries(rt_gpu PUBLIC rt_core rt_scene CUDA::cudart)

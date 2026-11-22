# rt_render: links every enabled backend (INTERFACES section 0 "CMake targets").
add_executable(rt_render main.cpp)
target_link_libraries(rt_render PRIVATE rt_cpu rt_bvh rt_scene rt_image rt_bench CLI11::CLI11)
if(TARGET rt_gpu)
  target_link_libraries(rt_render PRIVATE rt_gpu)
  target_compile_definitions(rt_render PRIVATE RT_HAVE_CUDA_BACKEND=1)
endif()

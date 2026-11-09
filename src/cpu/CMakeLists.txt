add_library(rt_cpu STATIC normal_renderer.cpp)
target_link_libraries(rt_cpu PUBLIC rt_core rt_scene OpenMP::OpenMP_CXX)

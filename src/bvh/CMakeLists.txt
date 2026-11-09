add_library(rt_bvh STATIC bvh.cpp)
target_link_libraries(rt_bvh PUBLIC rt_core OpenMP::OpenMP_CXX)

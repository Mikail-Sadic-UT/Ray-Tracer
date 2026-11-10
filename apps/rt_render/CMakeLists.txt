# rt_render: links every enabled backend (INTERFACES section 0 "CMake targets").
add_executable(rt_render main.cpp)
target_link_libraries(rt_render PRIVATE rt_cpu rt_bvh rt_scene rt_image CLI11::CLI11)

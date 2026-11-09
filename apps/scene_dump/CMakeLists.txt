add_executable(scene_dump main.cpp)
target_link_libraries(scene_dump PRIVATE rt_cpu rt_image CLI11::CLI11)

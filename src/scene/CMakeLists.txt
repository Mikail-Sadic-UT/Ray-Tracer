add_library(rt_scene STATIC scene_io.cpp hash.cpp)
target_link_libraries(rt_scene PUBLIC rt_core nlohmann_json::nlohmann_json)

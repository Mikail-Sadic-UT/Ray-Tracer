add_library(rt_camera STATIC camera.cpp)
target_link_libraries(rt_camera PUBLIC rt_core nlohmann_json::nlohmann_json)

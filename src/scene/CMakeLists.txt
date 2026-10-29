add_library(rt_scene STATIC scene_io.cpp hash.cpp)
target_link_libraries(rt_scene PUBLIC rt_core nlohmann_json::nlohmann_json)

# BUILD.md requires contraction disabled for canonical scene-hash sources.
set_source_files_properties(hash.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# rt_bench: timers, run statistics, the run harness, machine identity, and the results writer
# (INTERFACES section 0 "CMake targets"). Host-only and CUDA-free; gpu_timer.hpp is a header that
# only CUDA targets include.
add_library(rt_bench STATIC stats.cpp harness.cpp)
target_link_libraries(rt_bench PUBLIC rt_core nlohmann_json::nlohmann_json)

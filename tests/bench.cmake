# Shared timing component storage tests (Arek).
rt_add_gtest(test_timings SOURCES test_timings.cpp)

# Timers, run statistics, harness, and results writer (Arek).
rt_add_gtest(test_bench SOURCES test_stats.cpp test_harness.cpp test_machine_info.cpp test_results.cpp LIBS rt_bench)

if(RT_ENABLE_CUDA)
  # test_gpu_timer: CUDA event and stage timers. Skips the kernel tests without a device.
  rt_add_gtest(test_gpu_timer SOURCES test_gpu_timer.cu LIBS rt_bench CUDA::cudart)
endif()

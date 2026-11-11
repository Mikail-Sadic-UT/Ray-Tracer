# Shared timing component storage tests (Arek).
rt_add_gtest(test_timings SOURCES test_timings.cpp)

# Timers, run statistics, harness, and results writer (Arek).
rt_add_gtest(test_bench SOURCES test_stats.cpp test_harness.cpp LIBS rt_bench)

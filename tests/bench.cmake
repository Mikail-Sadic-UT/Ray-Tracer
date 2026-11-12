# Shared timing component storage tests (Arek).
rt_add_gtest(test_timings SOURCES test_timings.cpp)

# Timers, run statistics, harness, and results writer (Arek).
rt_add_gtest(test_bench SOURCES test_stats.cpp test_harness.cpp test_machine_info.cpp test_results.cpp LIBS rt_bench)

# test_results_fixtures: the committed results fixtures through rt::validate_results. The Python
# validator in scripts/bench/ is checked against the same files by results_schema_py.
rt_add_gtest(test_results_fixtures SOURCES test_results_fixtures.cpp LIBS rt_bench)
target_compile_definitions(test_results_fixtures PRIVATE
  RT_TEST_BENCH_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data/bench")

add_test(NAME results_schema_py COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_results_schema.py"
  --scripts "${PROJECT_SOURCE_DIR}/scripts/bench" --data "${PROJECT_SOURCE_DIR}/tests/data/bench")

# run_tapp: T_app aggregation in scripts/bench/run_tapp.py, on fixture results files.
add_test(NAME run_tapp_py COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_run_tapp.py"
  --scripts "${PROJECT_SOURCE_DIR}/scripts/bench" --data "${PROJECT_SOURCE_DIR}/tests/data/bench")

if(RT_ENABLE_CUDA)
  # test_gpu_timer: CUDA event and stage timers. Skips the kernel tests without a device.
  rt_add_gtest(test_gpu_timer SOURCES test_gpu_timer.cu LIBS rt_bench CUDA::cudart)
endif()

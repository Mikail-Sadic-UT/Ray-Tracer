rt_add_gtest(test_compare SOURCES test_compare.cpp LIBS rt_image rt_cpu)
target_compile_definitions(test_compare PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

rt_add_gtest(test_cpu SOURCES test_cpu.cpp LIBS rt_cpu rt_image)
target_compile_definitions(test_cpu PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

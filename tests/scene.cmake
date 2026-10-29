rt_add_gtest(test_scene SOURCES test_scene.cpp test_scene_io.cpp LIBS rt_scene)
target_compile_definitions(test_scene PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

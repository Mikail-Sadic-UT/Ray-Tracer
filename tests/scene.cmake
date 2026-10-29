rt_add_gtest(test_scene SOURCES test_scene.cpp test_scene_io.cpp LIBS rt_scene)
target_compile_definitions(test_scene PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

add_test(NAME scene_dump_known COMMAND scene_dump --verify
  "${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json")
set_tests_properties(scene_dump_known PROPERTIES
  PASS_REGULAR_EXPRESSION "scene_hash: 0xcbed9151089614c9")
add_test(NAME scene_dump_missing COMMAND scene_dump "${CMAKE_CURRENT_BINARY_DIR}/missing.json")
set_tests_properties(scene_dump_missing PROPERTIES WILL_FAIL TRUE)

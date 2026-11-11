rt_add_gtest(test_cpu SOURCES test_cpu.cpp LIBS rt_cpu rt_image)
target_compile_definitions(test_cpu PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

add_test(NAME scene_tools_cli COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_tools_cli.py"
  --render $<TARGET_FILE:rt_render>
  --dump $<TARGET_FILE:scene_dump> --compare $<TARGET_FILE:rt_compare>
  --generator $<TARGET_FILE:rt_gen>
  --scene "${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json")

# test_backend: CpuBackend through the Backend interface (Mikail).
rt_add_gtest(test_backend SOURCES test_backend.cpp LIBS rt_cpu rt_bvh rt_scene)
target_compile_definitions(test_backend PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

# rt_render_cli: end-to-end rt_render runs, byte-compared with scene_dump --render-normal.
add_test(NAME rt_render_cli COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_rt_render_cli.py"
  --render $<TARGET_FILE:rt_render> --dump $<TARGET_FILE:scene_dump>
  --compare $<TARGET_FILE:rt_compare>
  --scene "${PROJECT_SOURCE_DIR}/scenes/known/known_two_spheres.json")

# test_golden: lit regression goldens for the known scenes (Arek). Regression-only: see
# scenes/golden/README.md.
rt_add_gtest(test_golden SOURCES test_golden.cpp LIBS rt_cpu rt_bvh rt_scene rt_image)
target_compile_definitions(test_golden PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known"
  RT_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/scenes/golden")

# golden_cli: rt_render --shade lit on each known scene, compared with its golden by rt_compare,
# so the CLI path (--aov file naming, PFM round trip) is covered as well.
add_test(NAME golden_cli COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_golden_cli.py"
  --render $<TARGET_FILE:rt_render> --compare $<TARGET_FILE:rt_compare>
  --scenes "${PROJECT_SOURCE_DIR}/scenes/known" --goldens "${PROJECT_SOURCE_DIR}/scenes/golden")

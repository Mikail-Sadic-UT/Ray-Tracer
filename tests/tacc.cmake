# sweep_py: scripts/tacc/sweep.py presets, environment checks, and a small real CPU sweep
# through rt_render (Mikail, C12).
add_test(NAME sweep_py COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_sweep.py"
  --scripts "${PROJECT_SOURCE_DIR}/scripts/tacc" --render $<TARGET_FILE:rt_render>)
# extra_py: scripts/tacc/extra.py (CPU against GPU on the extra scenes, walkthrough timing) and
# scripts/bench/make_showcase.py and make_balls.py, with a tiny scene and path on the CPU backend.
add_test(NAME extra_py COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_extra.py"
  --scripts "${PROJECT_SOURCE_DIR}/scripts" --render $<TARGET_FILE:rt_render>)

# sweep_py: scripts/tacc/sweep.py presets, environment checks, and a small real CPU sweep
# through rt_render (Mikail, C12).
add_test(NAME sweep_py COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_sweep.py"
  --scripts "${PROJECT_SOURCE_DIR}/scripts/tacc" --render $<TARGET_FILE:rt_render>)

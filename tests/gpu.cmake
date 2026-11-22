if(RT_ENABLE_CUDA)
  # test_gpu: CudaBackend and DeviceScene through the Backend interface, compared with the CPU
  # backend under rt_compare (Mikail). Device cases skip without a visible GPU.
  rt_add_gtest(test_gpu SOURCES test_gpu.cpp LIBS rt_gpu rt_cpu rt_bvh rt_scene rt_image)
  target_compile_definitions(test_gpu PRIVATE
    RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")
endif()

if(RT_ENABLE_CUDA)
  # rt_render_cuda_cli: rt_render --backend cuda against --backend cpu on the known scenes,
  # repeat determinism, and the CUDA results fields. Exits 77 (skipped) without a GPU.
  add_test(NAME rt_render_cuda_cli
    COMMAND python3 "${PROJECT_SOURCE_DIR}/tests/test_rt_render_cuda_cli.py"
      --render $<TARGET_FILE:rt_render> --compare $<TARGET_FILE:rt_compare>
      --scenes "${PROJECT_SOURCE_DIR}/scenes/known")
  set_tests_properties(rt_render_cuda_cli PROPERTIES SKIP_RETURN_CODE 77)
endif()

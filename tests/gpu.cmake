if(RT_ENABLE_CUDA)
  # test_gpu: CudaBackend and DeviceScene through the Backend interface, compared with the CPU
  # backend under rt_compare (Mikail). Device cases skip without a visible GPU.
  rt_add_gtest(test_gpu SOURCES test_gpu.cpp LIBS rt_gpu rt_cpu rt_bvh rt_scene rt_image)
  target_compile_definitions(test_gpu PRIVATE
    RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")
endif()

rt_add_gtest(test_camera SOURCES test_camera.cpp test_camera_io.cpp test_camera_path.cpp LIBS rt_camera)

if(RT_ENABLE_CUDA)
  # test_camera_device: camera_ray and tan_det in a CUDA kernel, bit for bit against the host.
  rt_add_gtest(test_camera_device SOURCES test_camera_device.cu LIBS rt_camera CUDA::cudart)
endif()

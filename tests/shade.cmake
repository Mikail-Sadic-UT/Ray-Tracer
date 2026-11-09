# test_shade: shared shading policies (Arek).
rt_add_gtest(test_shade SOURCES test_shade.cpp)

# test_render_pixel: render_pixel with BruteQuery and NormalShading on the known scene.
rt_add_gtest(test_render_pixel SOURCES test_render_pixel.cpp LIBS rt_scene)
target_compile_definitions(test_render_pixel PRIVATE
  RT_TEST_SCENES_DIR="${PROJECT_SOURCE_DIR}/scenes/known")

if(RT_ENABLE_CUDA)
  # test_render_pixel_device: render_pixel in a CUDA kernel, compared with the host image.
  rt_add_gtest(test_render_pixel_device SOURCES test_render_pixel_device.cu LIBS CUDA::cudart)
endif()

# Each app directory with a CMakeLists.txt is built. hello_cuda builds only with CUDA.
foreach(app rt_render rt_gen rt_compare scene_dump obj2bin)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${app}/CMakeLists.txt)
    add_subdirectory(${app})
  endif()
endforeach()

if(RT_ENABLE_CUDA)
  add_subdirectory(hello_cuda)
endif()

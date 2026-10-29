add_library(rt_image STATIC image_io.cpp write_png.cpp)
target_link_libraries(rt_image PUBLIC rt_core PRIVATE stb)

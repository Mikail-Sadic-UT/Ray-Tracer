add_executable(hello_cuda main.cu)
target_link_libraries(hello_cuda PRIVATE rt_core CUDA::cudart)

# test_trace: intersection and brute-force tests (Mikail).
rt_add_gtest(test_trace SOURCES test_trace.cpp test_trace_brute_header.cpp)

# test_traverse: BVH traversal against brute force, on median BVHs from rt_bvh (Mikail).
rt_add_gtest(test_traverse SOURCES test_traverse.cpp LIBS rt_bvh)

if(RT_ENABLE_CUDA)
  # test_trace_device: runs the trace functions in CUDA kernels and compares with the host.
  rt_add_gtest(test_trace_device SOURCES test_trace_device.cu LIBS CUDA::cudart)
  # test_traverse_device: BvhQuery against BruteQuery in a CUDA kernel, and the device
  # overflow flag.
  rt_add_gtest(test_traverse_device SOURCES test_traverse_device.cu LIBS rt_bvh CUDA::cudart)
endif()

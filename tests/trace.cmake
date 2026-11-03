# test_trace: intersection and brute-force tests (Mikail).
rt_add_gtest(test_trace SOURCES test_trace.cpp test_trace_brute_header.cpp)

if(RT_ENABLE_CUDA)
  # test_trace_device: runs the trace functions in CUDA kernels and compares with the host.
  rt_add_gtest(test_trace_device SOURCES test_trace_device.cu LIBS CUDA::cudart)
endif()

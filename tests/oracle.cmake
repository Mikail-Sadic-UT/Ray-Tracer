# test_oracle: double-precision oracle for ray-sphere and ray-triangle, and its comparison with
# the float intersection code (Arek; BENCHMARKING quality protocol section 1).
rt_add_gtest(test_oracle SOURCES oracle/test_oracle_self.cpp oracle/test_oracle.cpp LIBS rt_scene)

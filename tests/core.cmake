# test_math: core math library tests (Arek). A plain main() runner until it is converted to
# GoogleTest (issue #5); ctest treats a non-zero exit code as a failure. After the conversion,
# replace these three lines with: rt_add_gtest(test_math SOURCES test_math.cpp)
add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE rt_core)
add_test(NAME test_math COMMAND test_math)

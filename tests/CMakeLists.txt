include(GoogleTest)

# test_build: GoogleTest smoke test for the build and CI setup.
add_executable(test_build test_build.cpp)
target_link_libraries(test_build PRIVATE rt_core GTest::gtest_main)
gtest_discover_tests(test_build DISCOVERY_MODE PRE_TEST)

# test_math: core math library tests. Plain main() runner until it is converted to
# GoogleTest (issue #5); ctest treats a non-zero exit code as a failure.
add_executable(test_math test_math.cpp)
target_link_libraries(test_math PRIVATE rt_core)
add_test(NAME test_math COMMAND test_math)

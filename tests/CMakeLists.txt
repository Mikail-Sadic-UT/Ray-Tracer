include(GoogleTest)

# rt_add_gtest(<target> SOURCES <files...> [LIBS <targets...>])
#
# Builds a GoogleTest executable that links rt_core and GTest::gtest_main, plus any module
# libraries listed in LIBS, and registers its test cases with ctest. Source paths are relative
# to this directory.
function(rt_add_gtest target)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE rt_core GTest::gtest_main ${ARG_LIBS})
  gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
endfunction()

# Each module owner keeps their test targets in tests/<module>.cmake (for example tests/scene.cmake
# calls rt_add_gtest(test_scene SOURCES test_scene.cpp LIBS rt_scene)). Every *.cmake file in
# this directory is included automatically, so adding tests never edits this shared file and
# two owners' PRs don't conflict here. Tests that need CUDA wrap their targets in
# if(RT_ENABLE_CUDA); the CPU-only build must always work.
file(GLOB rt_test_modules CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cmake)
foreach(module_file IN LISTS rt_test_modules)
  include(${module_file})
endforeach()

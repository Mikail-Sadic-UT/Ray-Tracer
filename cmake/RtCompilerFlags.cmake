# Compiler flags shared by every project target (BUILD.md "Compiler flags").
# rt_core carries them as INTERFACE options, so any target that links rt_core gets them.
# Include after enable_language(CUDA), so the CUDA flag variables exist when they are checked.

# No fast-math anywhere: the NaN-safe slab test depends on IEEE comparisons. Check the base flags
# and every configuration's flags (CMAKE_<LANG>_FLAGS_<CONFIG>, CONFIG in upper case) for C++
# and CUDA. This covers -D on the command line, presets, and CXXFLAGS/CUDAFLAGS from the
# environment.
set(_rt_configs DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
foreach(config IN LISTS CMAKE_BUILD_TYPE CMAKE_CONFIGURATION_TYPES)
  string(TOUPPER "${config}" config)
  list(APPEND _rt_configs ${config})
endforeach()
list(REMOVE_DUPLICATES _rt_configs)

foreach(lang CXX CUDA)
  foreach(var CMAKE_${lang}_FLAGS ${_rt_configs})
    if(NOT var MATCHES "^CMAKE_")
      set(var CMAKE_${lang}_FLAGS_${var})
    endif()
    if("${${var}}" MATCHES "fast-math|fast_math|-Ofast")
      message(FATAL_ERROR "${var} contains fast-math or -Ofast, which this project forbids")
    endif()
  endforeach()
endforeach()
unset(_rt_configs)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  set(RT_ARCH_FLAG -mcpu=native)  # Grace; GCC 13.2.0 on Vista accepts it (login node, Oct 28)
else()
  set(RT_ARCH_FLAG -march=native)
endif()

set(RT_CXX_FLAGS -Wall -Wextra ${RT_ARCH_FLAG})
set(RT_CUDA_FLAGS -lineinfo "-Xcompiler=-Wall,-Wextra")

option(RT_PTXAS_VERBOSE "Pass -Xptxas -v to nvcc (register and local-memory report)" OFF)
if(RT_PTXAS_VERBOSE)
  list(APPEND RT_CUDA_FLAGS -Xptxas=-v)
endif()

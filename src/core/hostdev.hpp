/// @file hostdev.hpp
/// @brief Macros that mark shared functions as callable from both host and device code.
#pragma once

/// @def RT_HOSTDEV
/// Expands to `__host__ __device__` under nvcc and to nothing under a plain C++ compiler.
#ifdef __CUDACC__
#define RT_HOSTDEV __host__ __device__
#else
#define RT_HOSTDEV
#endif

/// @def RT_INLINE
/// Host/device inline function. Shared functions are defined inline in headers, so every
/// translation unit compiles its own copy.
#define RT_INLINE RT_HOSTDEV inline

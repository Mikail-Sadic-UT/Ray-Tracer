#pragma once

#ifdef __CUDACC__
#define RT_HOSTDEV __host__ __device__
#else
#define RT_HOSTDEV
#endif

#define RT_INLINE RT_HOSTDEV inline

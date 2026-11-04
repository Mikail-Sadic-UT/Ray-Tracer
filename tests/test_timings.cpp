/// @file test_timings.cpp
/// @brief Tests for the shared timing component storage.
#include <gtest/gtest.h>

#include <type_traits>

#include "bench/timings.hpp"

static_assert(std::is_trivially_copyable_v<rt::Timings>);

/// Checks that timing fields default to zero, can be assigned, and CUDA kernel diagnostics stay
/// out of the non-overlapping component totals.
TEST(Timings, ComponentsDefaultToZeroAndCanBeAssigned) {
    rt::Timings timings;

    EXPECT_DOUBLE_EQ(timings.args, 0.0);
    EXPECT_DOUBLE_EQ(timings.load, 0.0);
    EXPECT_DOUBLE_EQ(timings.cuda_init, 0.0);
    EXPECT_DOUBLE_EQ(timings.bounds, 0.0);
    EXPECT_DOUBLE_EQ(timings.build, 0.0);
    EXPECT_DOUBLE_EQ(timings.layout, 0.0);
    EXPECT_DOUBLE_EQ(timings.convert, 0.0);
    EXPECT_DOUBLE_EQ(timings.alloc, 0.0);
    EXPECT_DOUBLE_EQ(timings.upload, 0.0);
    EXPECT_DOUBLE_EQ(timings.render, 0.0);
    EXPECT_DOUBLE_EQ(timings.render_kernel, 0.0);
    EXPECT_DOUBLE_EQ(timings.download, 0.0);

    timings.build = 2.5;
    timings.render = 4.0;
    timings.render_kernel = 100.0;
    timings.download = 1.0;
    EXPECT_DOUBLE_EQ(timings.build, 2.5);
    EXPECT_DOUBLE_EQ(timings.render, 4.0);

    const double t_br_components = timings.bounds + timings.build + timings.layout +
                                   timings.convert + timings.alloc + timings.upload +
                                   timings.render + timings.download;
    const double t_frame_components = timings.render + timings.download;
    EXPECT_DOUBLE_EQ(t_br_components, 7.5);
    EXPECT_DOUBLE_EQ(t_frame_components, 5.0);
}

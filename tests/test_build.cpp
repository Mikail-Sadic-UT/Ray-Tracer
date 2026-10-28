/// @file test_build.cpp
/// @brief Smoke tests for the build setup: GoogleTest runs, the src/ include root works, and
/// the compiler follows the IEEE rules the renderer depends on.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "core/math.hpp"

/// Headers resolve from the src/ include root and the shared math is usable on the host.
TEST(Build, CoreHeadersCompile) {
    const rt::vec3 v{1.0f, 2.0f, 2.0f};
    EXPECT_EQ(rt::length(v), 3.0f);
}

/// NaN comparisons are false and NaN != NaN, which fast-math would break.
TEST(Build, IeeeNanSemantics) {
    volatile float zero = 0.0f;
    const float nan = zero / zero;
    EXPECT_TRUE(std::isnan(nan));
    EXPECT_FALSE(nan < 1.0f);
    EXPECT_FALSE(nan > 1.0f);
    EXPECT_NE(nan, nan);
}

/// Division by a signed zero gives a signed infinity, as the slab test assumes.
TEST(Build, SignedZeroReciprocal) {
    volatile float pos = 0.0f;
    volatile float neg = -0.0f;
    EXPECT_EQ(1.0f / pos, std::numeric_limits<float>::infinity());
    EXPECT_EQ(1.0f / neg, -std::numeric_limits<float>::infinity());
}

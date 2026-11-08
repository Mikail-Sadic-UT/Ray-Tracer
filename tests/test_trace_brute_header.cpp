/// @file test_trace_brute_header.cpp
/// @brief Checks that trace/brute.hpp compiles on its own, and the BruteQuery layout.
// brute.hpp must stay the first include: that is what makes this file a self-containment
// check. test_trace.cpp cannot do it, because it includes trace/hit.hpp first.
#include "trace/brute.hpp"
// Other headers follow.
#include <gtest/gtest.h>

namespace {

/// BruteQuery holds only the scene view, so passing it to a kernel costs one Scene copy.
TEST(BruteQuery, Layout) {
    EXPECT_EQ(sizeof(rt::BruteQuery), sizeof(rt::Scene));
}

}  // namespace

/// @file test_trace.cpp
/// @brief Tests for src/trace: the Hit record.
// hit.hpp comes first, so this file also checks that it compiles on its own.
#include "trace/hit.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cstddef>

namespace {

/// Hit members sit at the offsets INTERFACES §5 fixes, in a 16-byte record.
TEST(Hit, Layout) {
    EXPECT_EQ(offsetof(rt::Hit, t), 0u);
    EXPECT_EQ(offsetof(rt::Hit, prim), 4u);
    EXPECT_EQ(offsetof(rt::Hit, u), 8u);
    EXPECT_EQ(offsetof(rt::Hit, v), 12u);
    EXPECT_EQ(sizeof(rt::Hit), 16u);
}

/// The miss constant is the all-ones PrimRef from INTERFACES §5.
TEST(Hit, MissPrim) {
    EXPECT_EQ(rt::RT_MISS_PRIM, 0xFFFFFFFFu);
}

}  // namespace

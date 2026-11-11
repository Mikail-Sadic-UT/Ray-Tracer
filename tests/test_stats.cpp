/// @file test_stats.cpp
/// @brief Tests for run statistics (median, min, max, spread, median run index).
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "bench/stats.hpp"
#include "core/error.hpp"

/// Checks the five-run case the protocol uses, with the median run in the middle of the order.
TEST(Stats, FiveRunsGiveMedianSpreadAndIndex) {
    const rt::RunStats s = rt::summarize({10.0, 12.0, 9.0, 11.0, 14.0});
    EXPECT_DOUBLE_EQ(s.median_ms, 11.0);
    EXPECT_DOUBLE_EQ(s.min_ms, 9.0);
    EXPECT_DOUBLE_EQ(s.max_ms, 14.0);
    EXPECT_DOUBLE_EQ(s.spread, (14.0 - 9.0) / 11.0);
    EXPECT_EQ(s.median_run_index, 3u);
}

/// Checks that the input order does not change the statistics, only the run index.
TEST(Stats, OrderOfRunsDoesNotChangeTheMedian) {
    const rt::RunStats a = rt::summarize({1.0, 2.0, 3.0, 4.0, 5.0});
    const rt::RunStats b = rt::summarize({5.0, 3.0, 1.0, 4.0, 2.0});
    EXPECT_DOUBLE_EQ(a.median_ms, b.median_ms);
    EXPECT_DOUBLE_EQ(a.spread, b.spread);
    EXPECT_EQ(a.median_run_index, 2u);
    EXPECT_EQ(b.median_run_index, 1u);
}

/// Checks the even-count rule: mean of the two middle totals, and the lower-middle run.
TEST(Stats, EvenCountUsesMeanOfMiddleTwoAndLowerRun) {
    const rt::RunStats s = rt::summarize({8.0, 2.0, 6.0, 4.0});
    EXPECT_DOUBLE_EQ(s.median_ms, 5.0);
    EXPECT_EQ(s.median_run_index, 3u);  // total 4.0 is the lower of the middle two
    EXPECT_DOUBLE_EQ(s.min_ms, 2.0);
    EXPECT_DOUBLE_EQ(s.max_ms, 8.0);
}

/// Checks that one run is its own median with zero spread.
TEST(Stats, SingleRun) {
    const rt::RunStats s = rt::summarize({7.5});
    EXPECT_DOUBLE_EQ(s.median_ms, 7.5);
    EXPECT_DOUBLE_EQ(s.min_ms, 7.5);
    EXPECT_DOUBLE_EQ(s.max_ms, 7.5);
    EXPECT_DOUBLE_EQ(s.spread, 0.0);
    EXPECT_EQ(s.median_run_index, 0u);
}

/// Checks that equal totals are ordered by run index when picking the median run.
TEST(Stats, TiesAreOrderedByRunIndex) {
    const rt::RunStats s = rt::summarize({3.0, 3.0, 3.0, 3.0, 3.0});
    EXPECT_DOUBLE_EQ(s.spread, 0.0);
    EXPECT_EQ(s.median_run_index, 2u);  // the middle slot of the stable order
    const rt::RunStats t = rt::summarize({5.0, 1.0, 5.0, 5.0, 9.0});
    EXPECT_DOUBLE_EQ(t.median_ms, 5.0);
    EXPECT_EQ(t.median_run_index, 2u);  // order: 1.0 (run 1), 5.0 (runs 0, 2, 3), 9.0 (run 4)
}

/// Checks that a zero median gives a zero spread instead of a division by zero.
TEST(Stats, ZeroMedianGivesZeroSpread) {
    const rt::RunStats s = rt::summarize({0.0, 0.0, 4.0});
    EXPECT_DOUBLE_EQ(s.median_ms, 0.0);
    EXPECT_DOUBLE_EQ(s.spread, 0.0);
    EXPECT_TRUE(std::isfinite(s.spread));
}

/// Checks that the median is exact for a spread known by hand (the 10% rerun threshold).
TEST(Stats, SpreadMatchesHandComputedValue) {
    const rt::RunStats s = rt::summarize({100.0, 100.0, 100.0, 105.0, 110.0});
    EXPECT_DOUBLE_EQ(s.median_ms, 100.0);
    EXPECT_DOUBLE_EQ(s.spread, 0.10);
}

/// Checks that bad input is rejected with an rt::Error naming the problem.
TEST(Stats, RejectsEmptyNegativeAndNonFiniteInput) {
    EXPECT_THROW(rt::summarize({}), rt::Error);
    EXPECT_THROW(rt::summarize({1.0, -0.5}), rt::Error);
    EXPECT_THROW(rt::summarize({1.0, std::numeric_limits<double>::quiet_NaN()}), rt::Error);
    EXPECT_THROW(rt::summarize({std::numeric_limits<double>::infinity()}), rt::Error);
    try {
        rt::summarize({1.0, -0.5});
        FAIL() << "expected rt::Error";
    } catch (const rt::Error& error) {
        EXPECT_NE(std::string(error.what()).find("run 1"), std::string::npos) << error.what();
    }
}

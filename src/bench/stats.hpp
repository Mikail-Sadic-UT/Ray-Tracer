/// @file stats.hpp
/// @brief Median, spread and representative run of a set of timed runs (INTERFACES section 8).
#pragma once

#include <cstddef>
#include <vector>

namespace rt {

/// Summary of the total times of one set of runs, in milliseconds.
struct RunStats {
    double median_ms = 0.0;            ///< Median total.
    double min_ms = 0.0;               ///< Fastest total.
    double max_ms = 0.0;               ///< Slowest total.
    double spread = 0.0;               ///< `(max - min) / median`; 0 when the median is 0.
    std::size_t median_run_index = 0;  ///< Run whose components explain the median total.
};

/// Summarizes the per-run totals @p totals_ms, which stay in run order.
///
/// For an odd count the median is the middle value. For an even count it is the mean of the two
/// middle values, and `median_run_index` is the run with the lower of the two. Runs are ordered by
/// total, and equal totals by run index; the median run is the one in the middle slot.
/// @throws Error if @p totals_ms is empty or holds a negative or non-finite value.
RunStats summarize(const std::vector<double>& totals_ms);

}  // namespace rt

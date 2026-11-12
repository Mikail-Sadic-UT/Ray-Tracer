/// @file stats.cpp
/// @brief Run statistics for the timing harness.
#include "bench/stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

#include "core/error.hpp"

namespace rt {

RunStats summarize(const std::vector<double>& totals_ms) {
    if (totals_ms.empty()) throw Error("bench: cannot summarize zero runs");
    for (std::size_t i = 0; i < totals_ms.size(); ++i) {
        if (!std::isfinite(totals_ms[i]) || totals_ms[i] < 0.0) {
            throw Error("bench: run " + std::to_string(i) + " has total " +
                        std::to_string(totals_ms[i]) + " ms, expected a finite, non-negative time");
        }
    }

    // Run indices ordered by total; stable, so equal totals keep run order.
    std::vector<std::size_t> order(totals_ms.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return totals_ms[a] < totals_ms[b]; });

    const std::size_t n = order.size();
    const std::size_t lower = (n - 1) / 2;
    const double lo_mid = totals_ms[order[lower]];
    const double hi_mid = totals_ms[order[n / 2]];

    RunStats stats;
    stats.median_ms = n % 2 == 1 ? lo_mid : lo_mid + (hi_mid - lo_mid) / 2.0;
    stats.min_ms = totals_ms[order.front()];
    stats.max_ms = totals_ms[order.back()];
    stats.spread = stats.median_ms > 0.0 ? (stats.max_ms - stats.min_ms) / stats.median_ms : 0.0;
    stats.median_run_index = order[lower];
    return stats;
}

}  // namespace rt

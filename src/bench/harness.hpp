/// @file harness.hpp
/// @brief Runs a workload repeatedly and records each run's total and components
///        (BENCHMARKING "Timing quantities", INTERFACES section 8).
///
/// The harness knows nothing about backends, scenes or images. The caller passes the work as
/// callables, so `rt_render`, tests and scripts all go through the same protocol.
#pragma once

#include <functional>
#include <vector>

#include "bench/stats.hpp"
#include "bench/timings.hpp"

namespace rt {

/// The three measured quantities (BENCHMARKING "Timing quantities").
enum class Quantity {
    App,          ///< T_app: first statement of `main()` to the image in host memory.
    BuildRender,  ///< T_br: start of preparation to the image in host memory.
    Frame,        ///< T_frame: start of `render()` to the image in host memory.
};

/// Sum of the components that belong to @p q.
///
/// `Frame` is `render + download`. `BuildRender` adds `bounds`, `build`, `layout`, `convert`,
/// `alloc` and `upload`. `App` adds `args`, `load` and `cuda_init`. `render_kernel` overlaps
/// `render`, so it is in no sum.
double component_sum(const Timings& t, Quantity q);

/// One measured run.
struct RunRecord {
    double total_ms = 0.0;         ///< Measured directly with its own timer.
    Timings components;            ///< What the stages reported, including `render_kernel`.
    double unattributed_ms = 0.0;  ///< `total_ms` minus the component sum; reported, not assumed 0.
};

/// The recorded runs of one quantity and their summary. Warm-up runs are not included.
struct RunSet {
    Quantity quantity = Quantity::BuildRender;  ///< Which component sum `unattributed_ms` uses.
    std::vector<RunRecord> runs;                ///< In execution order.
    RunStats stats;                             ///< Statistics of the run totals.
};

/// Warm-up and measured run counts. The protocol is one discarded warm-up, then five runs.
struct HarnessOptions {
    int warmup = 1;  ///< Runs executed and discarded first. At least 0.
    int runs = 5;    ///< Runs recorded. At least 1.
};

/// Measures T_br: for every warm-up and measured run, calls @p reset untimed, then times @p body.
///
/// @p reset must leave no preparation behind (for example `backend->release()` and dropping the
/// `Bvh`). @p body does the whole preparation and one frame: build the BVH, `prepare`, `render`.
/// It receives a zeroed Timings to fill. The total is the wall time of @p body alone, so work done
/// in @p reset is excluded and work @p body does but does not report shows up as unattributed.
/// @throws Error if the options are out of range. Exceptions from the callables propagate.
RunSet measure_build_and_render(const HarnessOptions& options, const std::function<void()>& reset,
                                const std::function<void(Timings&)>& body);

/// Measures T_frame: calls @p body (one `render`) for every warm-up and measured run.
/// @p body receives a zeroed Timings to fill.
/// @throws Error if the options are out of range. Exceptions from the callable propagate.
RunSet measure_frame(const HarnessOptions& options, const std::function<void(Timings&)>& body);

/// Builds the record of the single T_app run of a process from its directly measured total.
RunRecord make_app_record(double total_ms, const Timings& components);

}  // namespace rt

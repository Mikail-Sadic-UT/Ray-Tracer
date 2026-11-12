/// @file harness.cpp
/// @brief Run loops for T_br and T_frame.
#include "bench/harness.hpp"

#include <string>

#include "bench/timer.hpp"
#include "core/error.hpp"

namespace rt {

namespace {

void check_options(const HarnessOptions& options) {
    if (options.warmup < 0) {
        throw Error("bench: warmup = " + std::to_string(options.warmup) + " < 0");
    }
    if (options.runs < 1) {
        throw Error("bench: runs = " + std::to_string(options.runs) + " < 1");
    }
}

RunRecord make_record(double total_ms, const Timings& components, Quantity q) {
    RunRecord record;
    record.total_ms = total_ms;
    record.components = components;
    record.unattributed_ms = total_ms - component_sum(components, q);
    return record;
}

RunSet finish(Quantity q, std::vector<RunRecord> runs) {
    std::vector<double> totals;
    totals.reserve(runs.size());
    for (const RunRecord& run : runs) totals.push_back(run.total_ms);
    RunSet set;
    set.quantity = q;
    set.stats = summarize(totals);
    set.runs = std::move(runs);
    return set;
}

}  // namespace

double component_sum(const Timings& t, Quantity q) {
    const double frame = t.render + t.download;
    if (q == Quantity::Frame) return frame;
    const double build_render =
        t.bounds + t.build + t.layout + t.convert + t.alloc + t.upload + frame;
    if (q == Quantity::BuildRender) return build_render;
    return t.args + t.load + t.cuda_init + build_render;
}

RunSet measure_build_and_render(const HarnessOptions& options, const std::function<void()>& reset,
                                const std::function<void(Timings&)>& body) {
    check_options(options);
    std::vector<RunRecord> runs;
    runs.reserve(static_cast<std::size_t>(options.runs));
    for (int i = 0; i < options.warmup + options.runs; ++i) {
        reset();
        Timings components;
        WallTimer timer;
        body(components);
        const double total_ms = timer.elapsed_ms();
        if (i >= options.warmup) {
            runs.push_back(make_record(total_ms, components, Quantity::BuildRender));
        }
    }
    return finish(Quantity::BuildRender, std::move(runs));
}

RunSet measure_frame(const HarnessOptions& options, const std::function<void(Timings&)>& body) {
    check_options(options);
    std::vector<RunRecord> runs;
    runs.reserve(static_cast<std::size_t>(options.runs));
    for (int i = 0; i < options.warmup + options.runs; ++i) {
        Timings components;
        WallTimer timer;
        body(components);
        const double total_ms = timer.elapsed_ms();
        if (i >= options.warmup) {
            runs.push_back(make_record(total_ms, components, Quantity::Frame));
        }
    }
    return finish(Quantity::Frame, std::move(runs));
}

RunRecord make_app_record(double total_ms, const Timings& components) {
    return make_record(total_ms, components, Quantity::App);
}

}  // namespace rt

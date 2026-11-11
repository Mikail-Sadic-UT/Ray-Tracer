/// @file test_harness.cpp
/// @brief Tests for the wall-clock timers and the T_br / T_frame run loops.
#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "bench/harness.hpp"
#include "bench/timer.hpp"
#include "core/error.hpp"

namespace {

void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace

/// Checks that WallTimer never goes backward, measures at least a sleep, and restarts.
TEST(WallTimer, MeasuresElapsedTimeAndRestarts) {
    rt::WallTimer timer;
    const double first = timer.elapsed_ms();
    sleep_ms(15);
    const double second = timer.elapsed_ms();
    EXPECT_GE(first, 0.0);
    EXPECT_GE(second, first);
    EXPECT_GE(second, 14.0);
    timer.restart();
    EXPECT_LT(timer.elapsed_ms(), second);
}

/// Checks that ScopedTimer assigns (not adds) its scope's duration when it ends.
TEST(ScopedTimer, AssignsElapsedTimeToTheSlot) {
    const double sentinel = 1.0e9;  // far above any real scope time, even on a starved machine
    double slot = sentinel;
    {
        rt::ScopedTimer timer(slot);
        sleep_ms(10);
        EXPECT_DOUBLE_EQ(slot, sentinel);  // untouched until the scope ends
    }
    EXPECT_GE(slot, 9.0);
    EXPECT_LT(slot, sentinel);  // replaced, not added to
}

/// Checks which components each quantity sums, with a distinct power of two per field so any
/// extra or missing field changes the result.
TEST(Harness, ComponentSumsFollowTheTimingContract) {
    rt::Timings t;
    t.args = 1.0;
    t.load = 2.0;
    t.cuda_init = 4.0;
    t.bounds = 8.0;
    t.build = 16.0;
    t.layout = 32.0;
    t.convert = 64.0;
    t.alloc = 128.0;
    t.upload = 256.0;
    t.render = 512.0;
    t.render_kernel = 1024.0;  // overlaps render: in no sum
    t.download = 2048.0;

    EXPECT_DOUBLE_EQ(rt::component_sum(t, rt::Quantity::Frame), 512.0 + 2048.0);
    EXPECT_DOUBLE_EQ(rt::component_sum(t, rt::Quantity::BuildRender),
                     8.0 + 16.0 + 32.0 + 64.0 + 128.0 + 256.0 + 512.0 + 2048.0);
    EXPECT_DOUBLE_EQ(rt::component_sum(t, rt::Quantity::App),
                     1.0 + 2.0 + 4.0 + 8.0 + 16.0 + 32.0 + 64.0 + 128.0 + 256.0 + 512.0 + 2048.0);
}

/// Checks the warm-up and run counts, the call order, and that warm-up runs are not recorded.
TEST(Harness, BuildAndRenderDiscardsWarmupAndResetsBeforeEveryRun) {
    std::vector<std::string> calls;
    rt::HarnessOptions options;
    options.warmup = 2;
    options.runs = 3;
    const rt::RunSet set = rt::measure_build_and_render(
        options, [&] { calls.push_back("reset"); },
        [&](rt::Timings& t) {
            calls.push_back("body");
            t.render = static_cast<double>(calls.size());  // identifies the call
        });

    ASSERT_EQ(calls.size(), 10u);
    for (std::size_t i = 0; i < calls.size(); ++i) {
        EXPECT_EQ(calls[i], i % 2 == 0 ? "reset" : "body") << "call " << i;
    }
    ASSERT_EQ(set.runs.size(), 3u);
    EXPECT_DOUBLE_EQ(set.runs[0].components.render, 6.0);  // the third body call
    EXPECT_DOUBLE_EQ(set.runs[1].components.render, 8.0);
    EXPECT_DOUBLE_EQ(set.runs[2].components.render, 10.0);
    EXPECT_EQ(set.quantity, rt::Quantity::BuildRender);
}

/// Checks the default protocol: one warm-up, then five measured runs.
TEST(Harness, DefaultsAreOneWarmupThenFiveRuns) {
    int calls = 0;
    const rt::RunSet set = rt::measure_frame({}, [&](rt::Timings&) { ++calls; });
    EXPECT_EQ(calls, 6);
    EXPECT_EQ(set.runs.size(), 5u);
    EXPECT_EQ(set.quantity, rt::Quantity::Frame);
}

/// Checks that work in `reset` is not part of the measured total. The timer starts after `reset`
/// returns, so the total never exceeds the wall time from the end of `reset` to the end of the
/// call; a harness that timed `reset` would exceed it by the reset duration, however loaded the
/// machine is.
TEST(Harness, ResetIsNotTimed) {
    rt::HarnessOptions options;
    options.warmup = 0;
    options.runs = 1;
    rt::WallTimer since_reset_end;
    const rt::RunSet set = rt::measure_build_and_render(
        options,
        [&] {
            sleep_ms(100);
            since_reset_end.restart();
        },
        [](rt::Timings& t) {
            sleep_ms(3);
            t.render = 3.0;
        });
    const double window_ms = since_reset_end.elapsed_ms();
    ASSERT_EQ(set.runs.size(), 1u);
    EXPECT_GE(set.runs[0].total_ms, 2.9);
    EXPECT_LE(set.runs[0].total_ms, window_ms);
}

/// Checks that the total is measured directly: work the body does not report is unattributed,
/// and the unattributed value is exactly the total minus the component sum.
TEST(Harness, UnattributedTimeIsTotalMinusComponents) {
    rt::HarnessOptions options;
    options.warmup = 0;
    options.runs = 3;
    const rt::RunSet set = rt::measure_build_and_render(
        options, [] {},
        [](rt::Timings& t) {
            sleep_ms(20);
            t.build = 5.0;
            t.render = 5.0;
        });
    for (const rt::RunRecord& run : set.runs) {
        EXPECT_DOUBLE_EQ(run.unattributed_ms, run.total_ms - 10.0);
        EXPECT_GE(run.unattributed_ms, 9.0);
    }
}

/// Checks that every run starts from a zeroed Timings, so values never leak between runs.
TEST(Harness, EachRunGetsFreshTimings) {
    rt::HarnessOptions options;
    options.warmup = 1;
    options.runs = 3;
    int bodies = 0;
    const auto check_zero = [](const rt::Timings& t) {
        EXPECT_DOUBLE_EQ(t.render, 0.0);
        EXPECT_DOUBLE_EQ(t.download, 0.0);
        EXPECT_DOUBLE_EQ(t.render_kernel, 0.0);
    };
    rt::measure_frame(options, [&](rt::Timings& t) {
        check_zero(t);
        t.render = 9.0;
        t.download = 9.0;
        t.render_kernel = 9.0;
        ++bodies;
    });
    EXPECT_EQ(bodies, 4);
}

/// Checks that the summary matches the recorded totals, and its index names the median run.
TEST(Harness, StatsDescribeTheRecordedRuns) {
    int n = 0;
    const int sleeps[] = {4, 12, 8};
    rt::HarnessOptions options;
    options.warmup = 0;
    options.runs = 3;
    const rt::RunSet set = rt::measure_frame(options, [&](rt::Timings& t) {
        sleep_ms(sleeps[n++]);
        t.render = 1.0;
    });
    ASSERT_EQ(set.runs.size(), 3u);
    std::vector<double> totals;
    for (const rt::RunRecord& run : set.runs) totals.push_back(run.total_ms);
    const rt::RunStats expected = rt::summarize(totals);
    EXPECT_DOUBLE_EQ(set.stats.median_ms, expected.median_ms);
    EXPECT_DOUBLE_EQ(set.stats.spread, expected.spread);
    EXPECT_EQ(set.stats.median_run_index, expected.median_run_index);
    // For an odd count the median is one run's total. Sleep overshoot may reorder the runs, so
    // the index is checked against the data rather than against the sleep order.
    ASSERT_LT(set.stats.median_run_index, set.runs.size());
    EXPECT_DOUBLE_EQ(set.runs[set.stats.median_run_index].total_ms, set.stats.median_ms);
}

/// Checks the T_frame unattributed value uses only render and download.
TEST(Harness, FrameUnattributedUsesFrameComponents) {
    rt::HarnessOptions options;
    options.warmup = 0;
    options.runs = 1;
    const rt::RunSet set = rt::measure_frame(options, [](rt::Timings& t) {
        sleep_ms(5);
        t.bounds = 100.0;  // not a frame component
        t.render = 2.0;
        t.download = 1.0;
    });
    EXPECT_DOUBLE_EQ(set.runs[0].unattributed_ms, set.runs[0].total_ms - 3.0);
}

/// Checks the single T_app record: the total is the caller's, the sum includes startup stages.
TEST(Harness, AppRecordKeepsTheMeasuredTotal) {
    rt::Timings t;
    t.args = 1.0;
    t.load = 2.0;
    t.cuda_init = 3.0;
    t.build = 4.0;
    t.render = 5.0;
    t.download = 6.0;
    const rt::RunRecord record = rt::make_app_record(30.0, t);
    EXPECT_DOUBLE_EQ(record.total_ms, 30.0);
    EXPECT_DOUBLE_EQ(record.unattributed_ms, 30.0 - 21.0);
    EXPECT_DOUBLE_EQ(record.components.cuda_init, 3.0);
}

/// Checks that out-of-range options throw rt::Error before any callable runs.
TEST(Harness, RejectsBadOptions) {
    int calls = 0;
    const auto body = [&](rt::Timings&) { ++calls; };
    rt::HarnessOptions bad_runs;
    bad_runs.runs = 0;
    rt::HarnessOptions bad_warmup;
    bad_warmup.warmup = -1;
    EXPECT_THROW(rt::measure_frame(bad_runs, body), rt::Error);
    EXPECT_THROW(rt::measure_frame(bad_warmup, body), rt::Error);
    EXPECT_THROW(rt::measure_build_and_render(bad_runs, [] {}, body), rt::Error);
    EXPECT_EQ(calls, 0);
}

/// Checks that a failure in the workload stops the loop and reaches the caller unchanged.
TEST(Harness, ExceptionsFromTheBodyPropagate) {
    int calls = 0;
    EXPECT_THROW(rt::measure_frame({},
                                   [&](rt::Timings&) {
                                       if (++calls == 3) throw rt::Error("render: failed");
                                   }),
                 rt::Error);
    EXPECT_EQ(calls, 3);
}

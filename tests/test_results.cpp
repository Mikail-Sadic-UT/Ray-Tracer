/// @file test_results.cpp
/// @brief Tests for the results schema v2 writer and validator, on synthetic timings.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string>

#include "bench/results.hpp"
#include "core/error.hpp"

namespace {

using json = nlohmann::json;

/// A run with distinct component values so a swapped or dropped field changes the numbers.
rt::RunRecord synthetic_run(double total_ms, double render, double kernel = 0.0) {
    rt::Timings t;
    t.bounds = 1.0;
    t.build = 2.0;
    t.layout = 0.5;
    t.alloc = 0.25;
    t.upload = 0.75;
    t.render = render;
    t.render_kernel = kernel;
    t.download = 0.5;
    rt::RunRecord run;
    run.total_ms = total_ms;
    run.components = t;
    run.unattributed_ms = total_ms - rt::component_sum(t, rt::Quantity::BuildRender);
    return run;
}

rt::RunSet synthetic_build_render(double kernel = 0.0) {
    rt::RunSet set;
    set.quantity = rt::Quantity::BuildRender;
    const double totals[] = {10.0, 12.0, 9.0, 11.0, 14.0};
    for (double total : totals) set.runs.push_back(synthetic_run(total, total - 6.0, kernel));
    set.stats = rt::summarize({10.0, 12.0, 9.0, 11.0, 14.0});
    return set;
}

rt::RunSet synthetic_frame(double kernel = 0.0) {
    rt::RunSet set;
    set.quantity = rt::Quantity::Frame;
    const double totals[] = {4.0, 5.0, 4.5};
    for (double total : totals) {
        rt::Timings t;
        t.render = total - 0.5;
        t.render_kernel = kernel;
        t.download = 0.25;
        rt::RunRecord run;
        run.total_ms = total;
        run.components = t;
        run.unattributed_ms = total - rt::component_sum(t, rt::Quantity::Frame);
        set.runs.push_back(run);
    }
    set.stats = rt::summarize({4.0, 5.0, 4.5});
    return set;
}

rt::ResultsDoc base_doc() {
    rt::ResultsDoc doc;
    doc.machine.host = "node1";
    doc.machine.cpu_model = "Test CPU";
    doc.machine.cpu_cores = 8;
    doc.machine.gcc = "13.3.0";
    doc.machine.os = "Ubuntu 24.04";
    doc.build.commit = std::string(40, 'a');
    doc.build.cmake_flags = "CMAKE_BUILD_TYPE=Release";
    doc.scene.json = "scenes/known/known_two_spheres.json";
    doc.scene.hash = 0xcbed9151089614c9ull;
    doc.scene.num_spheres = 2;
    doc.scene.num_triangles = 2;
    doc.scene.num_primitives = 4;
    doc.scene.num_lights = 1;
    doc.config.backend = "cpu";
    doc.config.pipeline = "none";
    doc.config.bvh = "median";
    doc.config.build_threads = 4;
    doc.config.render_threads = 4;
    doc.config.width = 640;
    doc.config.height = 480;
    doc.config.aovs = {"primid", "lightmask"};
    doc.config.camera.position = {0.0f, 0.1f, 6.0f};
    doc.config.camera.look_at = {0.0f, 0.0f, 0.0f};
    doc.config.camera.vfov_deg = 45.0f;
    doc.config.camera.overridden = {"position"};
    doc.image.path = "out.pfm";
    doc.image.rgb_hash = 0x1234;
    return doc;
}

rt::ResultsDoc prepared_doc(double kernel = 0.0) {
    rt::ResultsDoc doc = base_doc();
    doc.build_and_render = synthetic_build_render(kernel);
    doc.frame = synthetic_frame(kernel);
    doc.bvh_stats = rt::BvhStatsInfo{7, 4, 3, 0, 12.5};
    return doc;
}

/// Applies @p mutate to the valid JSON and expects validation to fail naming @p field.
void expect_rejected(const std::function<void(json&)>& mutate, const std::string& field) {
    json results = rt::to_json(prepared_doc());
    ASSERT_NO_THROW(rt::validate_results(results));
    mutate(results);
    try {
        rt::validate_results(results);
        ADD_FAILURE() << "expected a validation error naming " << field;
    } catch (const rt::Error& error) {
        EXPECT_NE(std::string(error.what()).find(field), std::string::npos)
            << "message was: " << error.what();
    }
}

}  // namespace

/// Checks FNV-1a over float bits against values computed independently in Python.
TEST(Results, HashRgbMatchesIndependentFnv1a) {
    EXPECT_EQ(rt::hash_rgb(nullptr, 0), 0xcbf29ce484222325ull);
    const float zero = 0.0f;
    EXPECT_EQ(rt::hash_rgb(&zero, 1), 0x4d25767f9dce13f5ull);
    const float pair[] = {1.0f, -2.5f};
    EXPECT_EQ(rt::hash_rgb(pair, 2), 0x09e629ee2dfdb3f8ull);
}

/// Checks the fixed-width hash format.
TEST(Results, Hex64HasSixteenDigits) {
    EXPECT_EQ(rt::hex64(0), "0x0000000000000000");
    EXPECT_EQ(rt::hex64(0xcbed9151089614c9ull), "0xcbed9151089614c9");
    EXPECT_EQ(rt::hex64(~0ull), "0xffffffffffffffff");
}

/// Checks that a prepared-mode document validates and reports the right median, spread and index
/// for the synthetic runs (10, 12, 9, 11, 14 ms and 4, 5, 4.5 ms).
TEST(Results, PreparedDocumentValidatesWithCorrectStatistics) {
    const json r = rt::to_json(prepared_doc());
    ASSERT_NO_THROW(rt::validate_results(r));
    EXPECT_EQ(r["schema_version"], 2);
    EXPECT_FALSE(r.contains("app"));

    const json& br = r["build_and_render"];
    EXPECT_DOUBLE_EQ(br["median_ms"].get<double>(), 11.0);
    EXPECT_DOUBLE_EQ(br["min_ms"].get<double>(), 9.0);
    EXPECT_DOUBLE_EQ(br["max_ms"].get<double>(), 14.0);
    EXPECT_DOUBLE_EQ(br["spread"].get<double>(), 5.0 / 11.0);
    EXPECT_EQ(br["median_run_index"], 3);
    ASSERT_EQ(br["runs"].size(), 5u);
    EXPECT_DOUBLE_EQ(br["runs"][3]["total_ms"].get<double>(), 11.0);
    // 1 + 2 + 0.5 + 0.25 + 0.75 + render(5) + 0.5 = 10; total 11
    EXPECT_DOUBLE_EQ(br["runs"][3]["unattributed_ms"].get<double>(), 1.0);

    const json& frame = r["frame"];
    EXPECT_DOUBLE_EQ(frame["median_ms"].get<double>(), 4.5);
    EXPECT_DOUBLE_EQ(frame["spread"].get<double>(), 1.0 / 4.5);
    EXPECT_EQ(frame["median_run_index"], 2);
    EXPECT_DOUBLE_EQ(frame["runs"][2]["unattributed_ms"].get<double>(), 0.25);
}

/// Checks that every run lists exactly its quantity's components, with zeros for unused stages.
TEST(Results, ComponentsAreExactlyTheQuantitysSet) {
    const json r = rt::to_json(prepared_doc());
    const json& br = r["build_and_render"]["runs"][0]["components"];
    EXPECT_EQ(br.size(), 8u);
    for (const char* key :
         {"bounds", "build", "layout", "convert", "alloc", "upload", "render", "download"}) {
        EXPECT_TRUE(br.contains(key)) << key;
    }
    EXPECT_DOUBLE_EQ(br["convert"].get<double>(), 0.0);
    EXPECT_FALSE(br.contains("args"));
    const json& frame = r["frame"]["runs"][0]["components"];
    EXPECT_EQ(frame.size(), 2u);
    EXPECT_TRUE(frame.contains("render") && frame.contains("download"));
}

/// Checks that render_kernel is a diagnostic only: empty on the CPU, present on CUDA, and never
/// a component.
TEST(Results, RenderKernelIsADiagnosticNotAComponent) {
    const json cpu = rt::to_json(prepared_doc(0.0));
    EXPECT_TRUE(cpu["build_and_render"]["runs"][0]["diagnostics"].empty());
    EXPECT_TRUE(cpu["frame"]["runs"][0]["diagnostics"].is_object());

    const json gpu = rt::to_json(prepared_doc(3.5));
    ASSERT_NO_THROW(rt::validate_results(gpu));
    EXPECT_DOUBLE_EQ(gpu["frame"]["runs"][0]["diagnostics"]["render_kernel"].get<double>(), 3.5);
    EXPECT_FALSE(gpu["frame"]["runs"][0]["components"].contains("render_kernel"));
    // The diagnostic is excluded from unattributed: 4.0 - (3.5 + 0.25) = 0.25 either way.
    EXPECT_DOUBLE_EQ(gpu["frame"]["runs"][0]["unattributed_ms"].get<double>(), 0.25);
}

/// Checks the app-mode document: only `app`, with startup stages and no diagnostics.
TEST(Results, AppDocumentHasOnlyTheAppSection) {
    rt::ResultsDoc doc = base_doc();
    rt::Timings t;
    t.args = 0.5;
    t.load = 20.0;
    t.cuda_init = 150.0;
    t.build = 30.0;
    t.render = 40.0;
    t.download = 2.0;
    doc.app = rt::make_app_record(250.0, t);
    const json r = rt::to_json(doc);
    ASSERT_NO_THROW(rt::validate_results(r));
    EXPECT_FALSE(r.contains("build_and_render"));
    EXPECT_FALSE(r.contains("frame"));
    EXPECT_FALSE(r.contains("bvh_stats"));
    EXPECT_FALSE(r["app"].contains("diagnostics"));
    EXPECT_EQ(r["app"]["components"].size(), 11u);
    EXPECT_DOUBLE_EQ(r["app"]["total_ms"].get<double>(), 250.0);
    EXPECT_DOUBLE_EQ(r["app"]["unattributed_ms"].get<double>(), 250.0 - 242.5);
}

/// Checks the header fields: hashes as 0x + 16 digits, short float text, null fresh_pages.
TEST(Results, HeaderFieldsFollowTheSchema) {
    const json r = rt::to_json(prepared_doc());
    EXPECT_EQ(r["scene"]["hash"], "0xcbed9151089614c9");
    EXPECT_EQ(r["image"]["rgb_hash"], "0x0000000000001234");
    EXPECT_TRUE(r["config"]["mem"]["fresh_pages"].is_null());
    EXPECT_EQ(r["config"]["camera"]["overridden"][0], "position");
    // 0.1f is printed as 0.1, not 0.10000000149011612.
    EXPECT_NE(r.dump().find("[0.0,0.1,6.0]"), std::string::npos) << r["config"]["camera"].dump();
    EXPECT_EQ(r["config"]["camera"]["vfov_deg"].dump(), "45.0");
}

/// Checks that the file is written as parseable, valid JSON and that its content round-trips.
TEST(Results, WritesAValidFile) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rt_results_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         ".json");
    rt::write_results(path.string(), prepared_doc());
    std::ifstream in(path);
    const json parsed = json::parse(in);
    EXPECT_NO_THROW(rt::validate_results(parsed));
    EXPECT_EQ(parsed, rt::to_json(prepared_doc()));
    std::filesystem::remove(path);
}

/// Checks that an invalid document writes nothing, and an unwritable path reports rt::Error.
TEST(Results, InvalidDocumentOrPathThrows) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rt_results_invalid.json";
    std::filesystem::remove(path);
    rt::ResultsDoc doc = prepared_doc();
    doc.scene.num_primitives = 99;
    EXPECT_THROW(rt::write_results(path.string(), doc), rt::Error);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_THROW(rt::write_results("/nonexistent-dir/results.json", prepared_doc()), rt::Error);
}

/// Checks that to_json refuses times that cannot be valid JSON numbers.
TEST(Results, ToJsonRejectsBadTimesAndWrongQuantities) {
    rt::ResultsDoc nan_doc = prepared_doc();
    nan_doc.frame->runs[1].total_ms = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(rt::to_json(nan_doc), rt::Error);

    rt::ResultsDoc negative = prepared_doc();
    negative.build_and_render->runs[0].components.build = -1.0;
    EXPECT_THROW(rt::to_json(negative), rt::Error);

    rt::ResultsDoc swapped = prepared_doc();
    swapped.frame->quantity = rt::Quantity::BuildRender;
    EXPECT_THROW(rt::to_json(swapped), rt::Error);

    rt::ResultsDoc bad_camera = prepared_doc();
    bad_camera.config.camera.position[1] = std::numeric_limits<float>::infinity();
    EXPECT_THROW(rt::to_json(bad_camera), rt::Error);
}

/// Checks that each kind of schema violation is rejected with a message naming the field.
TEST(Results, ValidatorNamesTheFirstBadField) {
    expect_rejected([](json& r) { r["schema_version"] = 1; }, "schema_version");
    expect_rejected([](json& r) { r.erase("machine"); }, "machine: missing");
    expect_rejected([](json& r) { r["machine"].erase("os"); }, "machine.os");
    expect_rejected([](json& r) { r["build"]["dirty"] = "no"; }, "build.dirty");
    expect_rejected([](json& r) { r["scene"]["hash"] = "0xABC"; }, "scene.hash");
    expect_rejected([](json& r) { r["scene"]["num_primitives"] = 5; }, "scene.num_primitives");
    expect_rejected([](json& r) { r["config"]["backend"] = ""; }, "config.backend");
    expect_rejected([](json& r) { r["config"]["camera"]["up"] = json::array({0, 1}); },
                    "config.camera.up");
    expect_rejected([](json& r) { r["config"]["mem"]["fresh_pages"] = "yes"; },
                    "config.mem.fresh_pages");
    expect_rejected([](json& r) { r["image"]["rgb_hash"] = "1234"; }, "image.rgb_hash");
    expect_rejected(
        [](json& r) {
            r.erase("build_and_render");
            r.erase("frame");
        },
        "at least one of");
    expect_rejected([](json& r) { r["bvh_stats"].erase("sah_cost"); }, "bvh_stats.sah_cost");
}

/// Checks the timing rules: component sets, diagnostics placement, unattributed identity, and
/// statistics that must match the runs.
TEST(Results, ValidatorEnforcesTheTimingRules) {
    expect_rejected([](json& r) { r["build_and_render"]["runs"][2]["components"].erase("render"); },
                    "build_and_render.runs[2].components.render: missing");
    expect_rejected(
        [](json& r) { r["build_and_render"]["runs"][0]["components"]["render_kernel"] = 1.0; },
        "render_kernel");
    expect_rejected([](json& r) { r["frame"]["runs"][0]["components"]["bounds"] = 1.0; },
                    "frame.runs[0]");
    expect_rejected([](json& r) { r["frame"]["runs"][0]["diagnostics"]["other"] = 1.0; },
                    "diagnostics.other");
    expect_rejected([](json& r) { r["frame"]["runs"][1].erase("diagnostics"); },
                    "frame.runs[1].diagnostics");
    expect_rejected([](json& r) { r["build_and_render"]["runs"][1]["unattributed_ms"] = 0.0; },
                    "unattributed_ms");
    expect_rejected([](json& r) { r["build_and_render"]["median_ms"] = 12.0; }, "median_ms");
    expect_rejected([](json& r) { r["build_and_render"]["spread"] = 0.0; }, "spread");
    expect_rejected([](json& r) { r["build_and_render"]["max_ms"] = 99.0; }, "max_ms");
    expect_rejected([](json& r) { r["build_and_render"]["median_run_index"] = 0; },
                    "median_run_index");
    expect_rejected([](json& r) { r["build_and_render"]["median_run_index"] = 9; },
                    "median_run_index");
    expect_rejected([](json& r) { r["build_and_render"]["runs"] = json::array(); }, "runs");
    expect_rejected([](json& r) { r["frame"]["runs"][0]["total_ms"] = -1.0; }, "total_ms");
}

/// Checks that a hand-edited app section is held to the same rules.
TEST(Results, ValidatorChecksTheAppSection) {
    rt::ResultsDoc doc = base_doc();
    rt::Timings t;
    t.load = 10.0;
    t.render = 5.0;
    doc.app = rt::make_app_record(20.0, t);
    json r = rt::to_json(doc);
    ASSERT_NO_THROW(rt::validate_results(r));
    json with_diagnostics = r;
    with_diagnostics["app"]["diagnostics"] = json::object();
    EXPECT_THROW(rt::validate_results(with_diagnostics), rt::Error);
    json missing = r;
    missing["app"]["components"].erase("cuda_init");
    EXPECT_THROW(rt::validate_results(missing), rt::Error);
    json wrong = r;
    wrong["app"]["unattributed_ms"] = 0.0;
    EXPECT_THROW(rt::validate_results(wrong), rt::Error);
}

/// Checks the flow `rt_render` follows: real harness runs, machine and build identity, and the
/// writer, ending in a file that parses and validates.
TEST(Results, HarnessOutputFeedsTheWriter) {
    rt::ResultsDoc doc = base_doc();
    doc.machine = rt::collect_machine_info();
    doc.build = rt::collect_build_info();
    rt::HarnessOptions options;
    options.runs = 3;
    doc.build_and_render = rt::measure_build_and_render(
        options, [] {},
        [](rt::Timings& t) {
            t.bounds = 0.1;
            t.build = 0.2;
            t.render = 0.3;
        });
    doc.frame = rt::measure_frame(options, [](rt::Timings& t) { t.render = 0.1; });
    const json r = rt::to_json(doc);
    ASSERT_NO_THROW(rt::validate_results(r));
    EXPECT_EQ(r["build_and_render"]["runs"].size(), 3u);
    EXPECT_EQ(r["frame"]["runs"].size(), 3u);
    EXPECT_GE(r["build_and_render"]["runs"][0]["unattributed_ms"].get<double>(), -0.6);
}

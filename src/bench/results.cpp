/// @file results.cpp
/// @brief Results schema v2 writer and validator.
#include "bench/results.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string_view>

#include "core/error.hpp"
#include "core/float_format.hpp"

namespace rt {

namespace {

using json = nlohmann::json;

const std::vector<const char*> kFrameComponents = {"render", "download"};
const std::vector<const char*> kBuildRenderComponents = {"bounds", "build",  "layout", "convert",
                                                         "alloc",  "upload", "render", "download"};
const std::vector<const char*> kAppComponents = {"args",   "load",   "cuda_init", "bounds",
                                                 "build",  "layout", "convert",   "alloc",
                                                 "upload", "render", "download"};

const std::vector<const char*>& component_names(Quantity q) {
    if (q == Quantity::Frame) return kFrameComponents;
    return q == Quantity::BuildRender ? kBuildRenderComponents : kAppComponents;
}

double component_value(const Timings& t, const std::string& name) {
    if (name == "args") return t.args;
    if (name == "load") return t.load;
    if (name == "cuda_init") return t.cuda_init;
    if (name == "bounds") return t.bounds;
    if (name == "build") return t.build;
    if (name == "layout") return t.layout;
    if (name == "convert") return t.convert;
    if (name == "alloc") return t.alloc;
    if (name == "upload") return t.upload;
    if (name == "render") return t.render;
    return t.download;
}

// ---- writing --------------------------------------------------------------------------------

double checked_time(double ms, const std::string& what) {
    if (!std::isfinite(ms) || ms < 0.0) {
        throw Error("results: " + what + " = " + std::to_string(ms) +
                    " ms, expected a finite, non-negative time");
    }
    return ms;
}

json float_array(const std::array<float, 3>& v, const std::string& what) {
    json out = json::array();
    for (float x : v) {
        if (!std::isfinite(x)) throw Error("results: " + what + " is not finite");
        out.push_back(shortest_decimal(x));
    }
    return out;
}

json run_json(const RunRecord& run, Quantity q, const std::string& path, bool diagnostics) {
    json components = json::object();
    for (const char* name : component_names(q)) {
        components[name] =
            checked_time(component_value(run.components, name), path + ".components." + name);
    }
    json out = {{"total_ms", checked_time(run.total_ms, path + ".total_ms")},
                {"components", components}};
    if (diagnostics) {
        json diag = json::object();
        if (run.components.render_kernel > 0.0) {
            diag["render_kernel"] =
                checked_time(run.components.render_kernel, path + ".diagnostics.render_kernel");
        }
        out["diagnostics"] = diag;
    }
    if (!std::isfinite(run.unattributed_ms)) throw Error("results: " + path + ".unattributed_ms");
    out["unattributed_ms"] = run.unattributed_ms;
    return out;
}

json run_set_json(const RunSet& set, Quantity expected, const std::string& path) {
    if (set.quantity != expected) {
        throw Error("results: " + path + " holds runs of the wrong quantity");
    }
    json runs = json::array();
    for (std::size_t i = 0; i < set.runs.size(); ++i) {
        runs.push_back(
            run_json(set.runs[i], expected, path + ".runs[" + std::to_string(i) + "]", true));
    }
    return {{"runs", runs},
            {"median_ms", checked_time(set.stats.median_ms, path + ".median_ms")},
            {"min_ms", checked_time(set.stats.min_ms, path + ".min_ms")},
            {"max_ms", checked_time(set.stats.max_ms, path + ".max_ms")},
            {"spread", checked_time(set.stats.spread, path + ".spread")},
            {"median_run_index", set.stats.median_run_index}};
}

// ---- validation -----------------------------------------------------------------------------

[[noreturn]] void fail(std::string_view path, const std::string& problem) {
    throw Error("results: " + std::string(path) + ": " + problem);
}

std::string join(std::string_view path, std::string_view key) {
    return path.empty() ? std::string(key) : std::string(path) + "." + std::string(key);
}

const json& member(const json& object, std::string_view path, const char* key) {
    if (!object.is_object()) fail(path, "expected an object");
    const auto it = object.find(key);
    if (it == object.end()) fail(join(path, key), "missing");
    return *it;
}

const json& object_at(const json& parent, std::string_view path, const char* key) {
    const json& value = member(parent, path, key);
    if (!value.is_object()) fail(join(path, key), "expected an object");
    return value;
}

const json& array_at(const json& parent, std::string_view path, const char* key) {
    const json& value = member(parent, path, key);
    if (!value.is_array()) fail(join(path, key), "expected an array");
    return value;
}

std::string string_at(const json& parent, std::string_view path, const char* key,
                      bool non_empty = false) {
    const json& value = member(parent, path, key);
    if (!value.is_string()) fail(join(path, key), "expected a string");
    if (non_empty && value.get<std::string>().empty()) fail(join(path, key), "must not be empty");
    return value.get<std::string>();
}

bool bool_at(const json& parent, std::string_view path, const char* key) {
    const json& value = member(parent, path, key);
    if (!value.is_boolean()) fail(join(path, key), "expected true or false");
    return value.get<bool>();
}

uint64_t uint_at(const json& parent, std::string_view path, const char* key) {
    const json& value = member(parent, path, key);
    // A freshly built document holds signed integers and a parsed file holds unsigned ones.
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0)) {
        fail(join(path, key), "expected a non-negative integer");
    }
    return value.get<uint64_t>();
}

double number_at(const json& parent, std::string_view path, const char* key) {
    const json& value = member(parent, path, key);
    if (!value.is_number()) fail(join(path, key), "expected a number");
    const double number = value.get<double>();
    if (!std::isfinite(number)) fail(join(path, key), "not finite");
    return number;
}

double time_at(const json& parent, std::string_view path, const char* key) {
    const double ms = number_at(parent, path, key);
    if (ms < 0.0) fail(join(path, key), "negative time");
    return ms;
}

void check_hex(const json& parent, std::string_view path, const char* key) {
    const std::string text = string_at(parent, path, key);
    bool ok = text.size() == 18 && text[0] == '0' && text[1] == 'x';
    for (std::size_t i = 2; ok && i < text.size(); ++i) {
        ok = (text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f');
    }
    if (!ok)
        fail(join(path, key), "expected 0x and 16 lower-case hex digits, got \"" + text + "\"");
}

bool near(double a, double b) {
    return std::fabs(a - b) <= 1e-9 * std::max({1.0, std::fabs(a), std::fabs(b)});
}

void check_vec3(const json& parent, std::string_view path, const char* key) {
    const json& v = array_at(parent, path, key);
    if (v.size() != 3) fail(join(path, key), "expected 3 numbers");
    for (const json& x : v) {
        if (!x.is_number() || !std::isfinite(x.get<double>())) {
            fail(join(path, key), "expected 3 finite numbers");
        }
    }
}

/// Checks one run: exact component set, optional diagnostics, and the unattributed identity.
double check_run(const json& run, const std::string& path, Quantity q, bool diagnostics) {
    const double total = time_at(run, path, "total_ms");
    const json& components = object_at(run, path, "components");
    const std::string components_path = join(path, "components");
    std::set<std::string> allowed;
    double sum = 0.0;
    for (const char* name : component_names(q)) {
        allowed.insert(name);
        sum += time_at(components, components_path, name);
    }
    for (auto it = components.begin(); it != components.end(); ++it) {
        if (it.key() == "render_kernel") {
            fail(join(components_path, "render_kernel"),
                 "a diagnostic overlaps render; it belongs in diagnostics");
        }
        if (allowed.count(it.key()) == 0)
            fail(join(components_path, it.key()), "unknown component");
    }
    if (diagnostics) {
        const json& diag = object_at(run, path, "diagnostics");
        for (auto it = diag.begin(); it != diag.end(); ++it) {
            if (it.key() != "render_kernel")
                fail(join(join(path, "diagnostics"), it.key()), "unknown diagnostic");
            time_at(diag, join(path, "diagnostics"), "render_kernel");
        }
    } else if (run.contains("diagnostics")) {
        fail(join(path, "diagnostics"), "the app section has no diagnostics");
    }
    const double unattributed = number_at(run, path, "unattributed_ms");
    if (!near(unattributed, total - sum)) {
        fail(join(path, "unattributed_ms"),
             "expected total_ms - sum of components = " + std::to_string(total - sum) + ", got " +
                 std::to_string(unattributed));
    }
    return total;
}

void check_run_set(const json& section, const std::string& path, Quantity q) {
    const json& runs = array_at(section, path, "runs");
    if (runs.empty()) fail(join(path, "runs"), "expected at least one run");
    std::vector<double> totals;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        totals.push_back(check_run(runs[i], path + ".runs[" + std::to_string(i) + "]", q, true));
    }
    const RunStats stats = summarize(totals);
    const struct {
        const char* key;
        double expected;
    } fields[] = {{"median_ms", stats.median_ms},
                  {"min_ms", stats.min_ms},
                  {"max_ms", stats.max_ms},
                  {"spread", stats.spread}};
    for (const auto& field : fields) {
        const double actual = time_at(section, path, field.key);
        if (!near(actual, field.expected)) {
            fail(join(path, field.key), "the runs give " + std::to_string(field.expected) +
                                            ", got " + std::to_string(actual));
        }
    }
    const uint64_t index = uint_at(section, path, "median_run_index");
    if (index >= runs.size()) fail(join(path, "median_run_index"), "no such run");
    if (index != stats.median_run_index) {
        fail(join(path, "median_run_index"), "the runs give " +
                                                 std::to_string(stats.median_run_index) + ", got " +
                                                 std::to_string(index));
    }
}

}  // namespace

uint64_t hash_rgb(const float* rgb, std::size_t count) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < count; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &rgb[i], sizeof bits);
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (bits >> (8 * byte)) & 0xffu;
            hash *= 0x100000001b3ull;
        }
    }
    return hash;
}

std::string hex64(uint64_t value) {
    char text[19];
    std::snprintf(text, sizeof text, "0x%016llx", static_cast<unsigned long long>(value));
    return text;
}

json to_json(const ResultsDoc& doc) {
    const MachineInfo& m = doc.machine;
    const SceneInfo& s = doc.scene;
    const ConfigInfo& c = doc.config;
    const CameraInfo& cam = c.camera;

    json out;
    out["schema_version"] = RT_RESULTS_SCHEMA_VERSION;
    out["machine"] = {{"host", m.host},
                      {"cpu_model", m.cpu_model},
                      {"cpu_cores", m.cpu_cores},
                      {"gpu_name", m.gpu_name},
                      {"gpu_uuid", m.gpu_uuid},
                      {"driver", m.driver},
                      {"cuda_runtime", m.cuda_runtime},
                      {"nvcc", m.nvcc},
                      {"gcc", m.gcc},
                      {"os", m.os}};
    out["build"] = {{"commit", doc.build.commit},
                    {"dirty", doc.build.dirty},
                    {"cmake_flags", doc.build.cmake_flags}};
    out["scene"] = {{"json", s.json},
                    {"hash", hex64(s.hash)},
                    {"generator_command", s.generator_command},
                    {"num_spheres", s.num_spheres},
                    {"num_triangles", s.num_triangles},
                    {"num_primitives", s.num_primitives},
                    {"num_lights", s.num_lights}};

    json camera = {{"position", float_array(cam.position, "config.camera.position")},
                   {"look_at", float_array(cam.look_at, "config.camera.look_at")},
                   {"up", float_array(cam.up, "config.camera.up")},
                   {"overridden", cam.overridden}};
    if (!std::isfinite(cam.vfov_deg)) throw Error("results: config.camera.vfov_deg is not finite");
    camera["vfov_deg"] = shortest_decimal(cam.vfov_deg);
    json mem = {{"mode", c.mem.mode},
                {"advice", c.mem.advice},
                {"prefetch", c.mem.prefetch},
                {"realloc_per_prepare", c.mem.realloc_per_prepare},
                {"geometry_reused", c.mem.geometry_reused},
                {"fresh_pages", c.mem.fresh_pages ? json(*c.mem.fresh_pages) : json(nullptr)}};
    out["config"] = {{"backend", c.backend},
                     {"pipeline", c.pipeline},
                     {"bvh", c.bvh},
                     {"max_leaf_size", c.max_leaf_size},
                     {"build_threads", c.build_threads},
                     {"render_threads", c.render_threads},
                     {"width", c.width},
                     {"height", c.height},
                     {"aovs", c.aovs},
                     {"camera", camera},
                     {"query", c.query},
                     {"shade", c.shade},
                     {"counters", c.counters},
                     {"mem", mem}};

    if (doc.app) out["app"] = run_json(*doc.app, Quantity::App, "app", false);
    if (doc.build_and_render) {
        out["build_and_render"] =
            run_set_json(*doc.build_and_render, Quantity::BuildRender, "build_and_render");
    }
    if (doc.frame) out["frame"] = run_set_json(*doc.frame, Quantity::Frame, "frame");
    if (doc.bvh_stats) {
        const BvhStatsInfo& b = *doc.bvh_stats;
        out["bvh_stats"] = {{"nodes", b.nodes},
                            {"leaves", b.leaves},
                            {"depth", b.depth},
                            {"fallbacks", b.fallbacks},
                            {"sah_cost", checked_time(b.sah_cost, "bvh_stats.sah_cost")}};
    }
    out["image"] = {{"path", doc.image.path}, {"rgb_hash", hex64(doc.image.rgb_hash)}};
    return out;
}

void validate_results(const json& results) {
    if (!results.is_object()) fail("", "expected a JSON object");
    if (uint_at(results, "", "schema_version") !=
        static_cast<uint64_t>(RT_RESULTS_SCHEMA_VERSION)) {
        fail("schema_version", "expected " + std::to_string(RT_RESULTS_SCHEMA_VERSION));
    }

    const json& machine = object_at(results, "", "machine");
    for (const char* key : {"host", "cpu_model", "gpu_name", "gpu_uuid", "driver", "cuda_runtime",
                            "nvcc", "gcc", "os"}) {
        string_at(machine, "machine", key);
    }
    uint_at(machine, "machine", "cpu_cores");

    const json& build = object_at(results, "", "build");
    string_at(build, "build", "commit");
    bool_at(build, "build", "dirty");
    string_at(build, "build", "cmake_flags");

    const json& scene = object_at(results, "", "scene");
    string_at(scene, "scene", "json");
    check_hex(scene, "scene", "hash");
    string_at(scene, "scene", "generator_command");
    const uint64_t spheres = uint_at(scene, "scene", "num_spheres");
    const uint64_t triangles = uint_at(scene, "scene", "num_triangles");
    if (uint_at(scene, "scene", "num_primitives") != spheres + triangles) {
        fail("scene.num_primitives",
             "expected num_spheres + num_triangles = " + std::to_string(spheres + triangles));
    }
    uint_at(scene, "scene", "num_lights");

    const json& config = object_at(results, "", "config");
    for (const char* key : {"backend", "bvh", "query", "shade"}) {
        string_at(config, "config", key, true);
    }
    string_at(config, "config", "pipeline");
    for (const char* key :
         {"max_leaf_size", "build_threads", "render_threads", "width", "height"}) {
        uint_at(config, "config", key);
    }
    for (const json& aov : array_at(config, "config", "aovs")) {
        if (!aov.is_string()) fail("config.aovs", "expected strings");
    }
    bool_at(config, "config", "counters");
    const json& camera = object_at(config, "config", "camera");
    for (const char* key : {"position", "look_at", "up"}) check_vec3(camera, "config.camera", key);
    number_at(camera, "config.camera", "vfov_deg");
    for (const json& name : array_at(camera, "config.camera", "overridden")) {
        if (!name.is_string()) fail("config.camera.overridden", "expected strings");
    }
    const json& mem = object_at(config, "config", "mem");
    string_at(mem, "config.mem", "mode", true);
    string_at(mem, "config.mem", "advice", true);
    for (const char* key : {"prefetch", "realloc_per_prepare", "geometry_reused"}) {
        bool_at(mem, "config.mem", key);
    }
    const json& fresh = member(mem, "config.mem", "fresh_pages");
    if (!fresh.is_null() && !fresh.is_boolean())
        fail("config.mem.fresh_pages", "expected true, false or null");

    bool measured = false;
    if (results.contains("app")) {
        measured = true;
        check_run(object_at(results, "", "app"), "app", Quantity::App, false);
    }
    if (results.contains("build_and_render")) {
        measured = true;
        check_run_set(object_at(results, "", "build_and_render"), "build_and_render",
                      Quantity::BuildRender);
    }
    if (results.contains("frame")) {
        measured = true;
        check_run_set(object_at(results, "", "frame"), "frame", Quantity::Frame);
    }
    if (!measured) fail("", "expected at least one of app, build_and_render or frame");

    if (results.contains("bvh_stats")) {
        const json& bvh = object_at(results, "", "bvh_stats");
        for (const char* key : {"nodes", "leaves", "depth", "fallbacks"}) {
            uint_at(bvh, "bvh_stats", key);
        }
        time_at(bvh, "bvh_stats", "sah_cost");
    }

    const json& image = object_at(results, "", "image");
    string_at(image, "image", "path");
    check_hex(image, "image", "rgb_hash");
}

void write_results(const std::string& path, const ResultsDoc& doc) {
    const json results = to_json(doc);
    validate_results(results);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw Error("results: cannot open '" + path + "' for writing");
    out << results.dump(2) << '\n';
    out.flush();
    if (!out) throw Error("results: writing '" + path + "' failed");
}

}  // namespace rt

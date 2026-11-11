/// @file results.hpp
/// @brief Results file, schema version 2 (INTERFACES section 8): plain structs that mirror the
///        JSON, a writer, and a validator.
///
/// The structs hold no scene, image or BVH types, so `rt_bench` depends only on `rt_core`.
/// `rt_render` fills them from its own objects. A process writes one results file with the
/// sections it measured: `app` in app mode, `build_and_render` and `frame` in prepared mode.
/// Sections that were not measured are omitted from the JSON, and so is `bvh_stats` when no
/// BVH was built.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "bench/harness.hpp"
#include "bench/machine_info.hpp"

namespace rt {

/// The scene that was rendered (`scene`).
struct SceneInfo {
    std::string json;               ///< Path of the scene JSON as given on the command line.
    uint64_t hash = 0;              ///< `SceneHost::hash`.
    std::string generator_command;  ///< Exact `rt_gen` command, or empty for a hand-written scene.
    uint32_t num_spheres = 0;
    uint32_t num_triangles = 0;
    uint32_t num_primitives = 0;  ///< Must equal `num_spheres + num_triangles`.
    uint32_t num_lights = 0;
};

/// The effective camera and which fields the command line overrode (`config.camera`).
struct CameraInfo {
    std::array<float, 3> position{};
    std::array<float, 3> look_at{};
    std::array<float, 3> up{0.0f, 1.0f, 0.0f};
    float vfov_deg = 0.0f;
    std::vector<std::string> overridden;  ///< Names such as "position"; empty if none.
};

/// GPU memory configuration (`config.mem`).
struct MemInfo {
    std::string mode = "explicit";  ///< explicit, managed, system or system-reuse.
    std::string advice = "none";    ///< none, preferred-gpu or read-mostly.
    bool prefetch = false;
    bool realloc_per_prepare = true;
    bool geometry_reused = false;
    std::optional<bool> fresh_pages;  ///< Only meaningful for `system`; null otherwise.
};

/// Effective run configuration (`config`).
struct ConfigInfo {
    std::string backend;   ///< cpu, cuda, embree or optix.
    std::string pipeline;  ///< mega, wavefront, or "none" for backends without one.
    std::string bvh;       ///< none, median, lbvh or sah.
    uint32_t max_leaf_size = 4;
    int build_threads = 1;
    int render_threads = 1;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<std::string> aovs;  ///< "primid" and/or "lightmask".
    CameraInfo camera;
    std::string query = "bvh";  ///< bvh or brute.
    std::string shade = "lit";  ///< lit or normal.
    bool counters = false;
    MemInfo mem;
};

/// Shape of the BVH that was built (`bvh_stats`).
struct BvhStatsInfo {
    uint32_t nodes = 0;
    uint32_t leaves = 0;
    uint32_t depth = 0;
    uint32_t fallbacks = 0;
    double sah_cost = 0.0;
};

/// The image that was produced (`image`).
struct ImageInfo {
    std::string path;       ///< Output path, or empty if none was written.
    uint64_t rgb_hash = 0;  ///< `hash_rgb` of the linear RGB buffer.
};

/// Everything one process reports.
struct ResultsDoc {
    MachineInfo machine;
    BuildInfo build;
    SceneInfo scene;
    ConfigInfo config;
    std::optional<RunRecord> app;            ///< T_app, from `make_app_record`. App mode only.
    std::optional<RunSet> build_and_render;  ///< T_br; its `quantity` must be BuildRender.
    std::optional<RunSet> frame;             ///< T_frame; its `quantity` must be Frame.
    std::optional<BvhStatsInfo> bvh_stats;   ///< Omitted when no BVH was built.
    ImageInfo image;
};

/// Value of the `schema_version` field.
inline constexpr int RT_RESULTS_SCHEMA_VERSION = 2;

/// FNV-1a 64-bit hash of @p count floats, each as its IEEE-754 bits in little-endian byte order,
/// so the value is the same on every host. Used for `image.rgb_hash`.
uint64_t hash_rgb(const float* rgb, std::size_t count);

/// Formats @p value as `0x` and 16 lower-case hex digits, the form of every hash in the file.
std::string hex64(uint64_t value);

/// Builds the schema v2 JSON object for @p doc.
///
/// Every component that belongs to a run's quantity is written, with 0 for stages the backend
/// does not use. `render_kernel` goes only into `diagnostics`, and only when it is positive, so
/// CPU runs have an empty `diagnostics`. Each `frame` run also carries `unattributed_ms`, and
/// `frame` carries `median_run_index`, so every section reads the same way.
/// @throws Error if a time is negative or not finite, or a RunSet has the wrong quantity.
nlohmann::json to_json(const ResultsDoc& doc);

/// Checks that @p results follows schema v2: required keys and types, hash format, primitive
/// count, exact component sets, `render_kernel` only in `diagnostics`, `unattributed_ms` equal to
/// the total minus the component sum, and `median_ms`, `min_ms`, `max_ms`, `spread` and
/// `median_run_index` equal to what the run totals give.
/// @throws Error naming the first field that fails, for example
///         `results: build_and_render.runs[2].components.render: missing`.
void validate_results(const nlohmann::json& results);

/// Validates @p doc, then writes it to @p path as indented JSON. Nothing is written if the
/// document is invalid.
/// @throws Error if the document is invalid or the file cannot be written.
void write_results(const std::string& path, const ResultsDoc& doc);

}  // namespace rt

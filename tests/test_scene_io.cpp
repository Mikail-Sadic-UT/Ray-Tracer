/// @file test_scene_io.cpp
/// @brief Inline JSON, validation, file errors, and canonical hashing checks.
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "scene/hash.hpp"
#include "scene/scene_io.hpp"

namespace {

using nlohmann::json;

/// Requires the project's error type and a diagnostic naming the offending field.
void expect_error(const std::function<void()>& action, const std::string& field) {
    try {
        action();
        FAIL() << "Expected rt::Error naming " << field;
    } catch (const rt::Error& error) {
        EXPECT_NE(std::string(error.what()).find(field), std::string::npos) << error.what();
    }
}

/// Real known-scene fixture and isolated temporary storage for file error tests.
class SceneIo : public testing::Test {
   protected:
    void SetUp() override {
        std::ifstream input(known_path());
        ASSERT_TRUE(input.is_open());
        input >> document;
        const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("rt-scene-" + std::to_string(id));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }

    void TearDown() override {
        if (!directory.empty()) std::filesystem::remove_all(directory);
    }

    static std::string known_path() {
        return std::string(RT_TEST_SCENES_DIR) + "/known_two_spheres.json";
    }

    json document;
    std::filesystem::path directory;
};

/// Checks actual file loading, field mapping, and the hash produced after validation.
TEST_F(SceneIo, LoadsKnownTwoSpheres) {
    const auto scene = rt::load_scene(known_path());
    ASSERT_EQ(scene.spheres.size(), 2u);
    ASSERT_EQ(scene.triangles.size(), 2u);
    ASSERT_EQ(scene.materials.size(), 3u);
    ASSERT_EQ(scene.lights.size(), 1u);
    EXPECT_EQ(scene.spheres[0].center.x, -0.75f);
    EXPECT_EQ(scene.spheres[1].material_id, 1u);
    EXPECT_EQ(scene.triangles[0].v1.z, 5.0f);
    EXPECT_EQ(scene.triangles[1].material_id, 2u);
    EXPECT_EQ(scene.materials[0].albedo.x, 0.8f);
    EXPECT_EQ(scene.materials[0].flags, 0u);
    EXPECT_EQ(scene.lights[0].type, rt::LIGHT_POINT);
    EXPECT_EQ(scene.lights[0].intensity.x, 20.0f);
    EXPECT_EQ(scene.lights[0].pad, 0.0f);
    EXPECT_EQ(scene.camera.width, 640u);
    EXPECT_EQ(scene.camera.height, 480u);
    EXPECT_EQ(scene.camera.position.z, 5.0f);
    EXPECT_EQ(scene.background.z, 0.15f);
    EXPECT_EQ(scene.ambient, 0.05f);
    EXPECT_EQ(scene.hash, rt::scene_hash(scene.view()));
    EXPECT_NE(scene.hash, 0u);
}

/// Schema errors and numeric conversions must not silently coerce or wrap invalid values.
TEST_F(SceneIo, RejectsMalformedFieldsWithPaths) {
    struct BadField {
        const char* pointer;
        json value;
        const char* diagnostic;
    };
    const std::vector<BadField> cases = {
        {"/version", 1, "version"},
        {"/version", 2.0, "version"},
        {"/camera", nullptr, "camera"},
        {"/camera/width", -1, "camera.width"},
        {"/camera/width", 4294967296ull, "camera.width"},
        {"/camera/width", 640.5, "camera.width"},
        {"/camera/height", true, "camera.height"},
        {"/camera/position", json::array({0, 1}), "camera.position"},
        {"/camera/position/0", "0", "camera.position[0]"},
        {"/background", json::array({0, 0, 0, 0}), "background"},
        {"/ambient", nullptr, "ambient"},
        {"/ambient", 1.0e100, "ambient"},
        {"/materials", json::object(), "materials"},
        {"/materials/0/specular", 0, "materials[0].specular"},
        {"/lights/0/type", "directional", "lights[0].type"},
        {"/lights/0/type", "unknown", "lights[0].type"},
        {"/geometry", nullptr, "geometry"},
        {"/geometry/spheres", nullptr, "geometry.spheres"},
        {"/geometry/spheres/1/radius", "1", "spheres[1].radius"},
        {"/geometry/spheres/0/material", -1, "spheres[0].material"},
        {"/geometry/spheres/0/material", 4294967296ull, "spheres[0].material"},
        {"/geometry/spheres/0/material", 0.5, "spheres[0].material"},
        {"/geometry/triangles/1/v", json::array(), "triangles[1].v"},
        {"/geometry/triangles/1/v/0", false, "triangles[1].v[0]"},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.pointer);
        auto invalid = document;
        invalid[json::json_pointer(test.pointer)] = test.value;
        expect_error([&] { rt::parse_scene(invalid); }, test.diagnostic);
    }
    document["materials"][0].erase("albedo");
    expect_error([&] { rt::parse_scene(document); }, "materials[0].albedo");
}

/// Shared validation also protects generated scenes and caller-side camera overrides.
TEST_F(SceneIo, RejectsInvalidSceneValues) {
    struct BadScene {
        std::function<void(rt::SceneHost&)> corrupt;
        const char* diagnostic;
    };
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<BadScene> cases = {
        {[](auto& s) { s.spheres[1].radius = 0; }, "spheres[1].radius"},
        {[](auto& s) { s.spheres[0].radius = -1; }, "spheres[0].radius"},
        {[&](auto& s) { s.spheres[0].center.x = inf; }, "spheres[0].center[0]"},
        {[&](auto& s) { s.spheres[0].radius = nan; }, "spheres[0].radius"},
        {[](auto& s) { s.spheres[0].material_id = 3; }, "spheres[0].material_id"},
        {[](auto& s) { s.triangles[1].material_id = 3; }, "triangles[1].material_id"},
        {[&](auto& s) { s.triangles[1].v2.z = nan; }, "triangles[1].v2[2]"},
        {[](auto& s) { s.materials.clear(); }, "spheres[0].material_id"},
        {[](auto& s) { s.materials[0].albedo.y = -0.1f; }, "materials[0].albedo[1]"},
        {[](auto& s) { s.materials[0].ks.z = -1; }, "materials[0].ks[2]"},
        {[&](auto& s) { s.materials[0].albedo.x = nan; }, "materials[0].albedo[0]"},
        {[&](auto& s) { s.materials[0].ks.y = inf; }, "materials[0].ks[1]"},
        {[](auto& s) { s.materials[0].shininess = 0; }, "materials[0].shininess"},
        {[&](auto& s) { s.materials[0].shininess = inf; }, "materials[0].shininess"},
        {[](auto& s) { s.materials[0].flags = 2; }, "materials[0].flags"},
        {[](auto& s) { s.lights[0].type = rt::LIGHT_DIRECTIONAL; }, "lights[0].type"},
        {[&](auto& s) { s.lights[0].pos_or_dir.x = inf; }, "lights[0].position[0]"},
        {[](auto& s) { s.lights[0].intensity.z = -1; }, "lights[0].intensity[2]"},
        {[&](auto& s) { s.lights[0].intensity.y = nan; }, "lights[0].intensity[1]"},
        {[](auto& s) { s.background.x = -1; }, "background[0]"},
        {[&](auto& s) { s.background.y = inf; }, "background[1]"},
        {[](auto& s) { s.ambient = -1; }, "ambient"},
        {[&](auto& s) { s.ambient = nan; }, "ambient"},
        {[](auto& s) { s.camera.look_at = s.camera.position; }, "camera.look_at"},
        {[](auto& s) { s.camera.up = {0, 0, 0}; }, "camera.up"},
        {[](auto& s) { s.camera.up = s.camera.position - s.camera.look_at; }, "camera.up"},
        {[](auto& s) { s.camera.vfov_deg = 0; }, "camera.vfov_deg"},
        {[](auto& s) { s.camera.vfov_deg = 180; }, "camera.vfov_deg"},
        {[&](auto& s) { s.camera.position.z = nan; }, "camera.position[2]"},
        {[&](auto& s) { s.camera.look_at.y = inf; }, "camera.look_at[1]"},
        {[&](auto& s) { s.camera.up.x = nan; }, "camera.up[0]"},
        {[&](auto& s) { s.camera.vfov_deg = inf; }, "camera.vfov_deg"},
        {[](auto& s) { s.camera.width = 0; }, "camera.width"},
        {[](auto& s) { s.camera.height = 16385; }, "camera.height"},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.diagnostic);
        auto invalid = rt::parse_scene(document);
        test.corrupt(invalid);
        expect_error([&] { rt::validate_scene(invalid); }, test.diagnostic);
    }
}

/// Loading must actually invoke numeric validation, rather than only checking JSON types.
TEST_F(SceneIo, ValidatesAfterParsing) {
    document["geometry"]["spheres"][1]["radius"] = -0.5;
    expect_error([&] { rt::parse_scene(document); }, "spheres[1].radius");
}

/// The cross-product threshold rejects near-parallel directions, not just exact parallelism.
TEST_F(SceneIo, ChecksNearParallelCameraUp) {
    auto scene = rt::parse_scene(document);
    scene.camera.position = {0, 0, 1};
    scene.camera.look_at = {0, 0, 0};
    scene.camera.up = {0, 1.0e-7f, 1};
    expect_error([&] { rt::validate_scene(scene); }, "camera.up");
    scene.camera.up = {0, 2.0e-6f, 1};
    EXPECT_NO_THROW(rt::validate_scene(scene));
}

/// Degenerate triangles, empty scenes, and point lights on surfaces are permitted.
TEST_F(SceneIo, AllowsContractEdgeCases) {
    auto scene = rt::parse_scene(document);
    scene.triangles[0].v1 = scene.triangles[0].v0;
    scene.lights[0].pos_or_dir = {0, 0, 0};
    scene.camera.width = 1;
    scene.camera.height = 16384;
    scene.materials[0].albedo = {2, 3, 4};
    EXPECT_NO_THROW(rt::validate_scene(scene));
    document["geometry"]["spheres"] = json::array();
    document["geometry"]["triangles"] = json::array();
    document["materials"] = json::array();
    document["lights"] = json::array();
    const auto empty = rt::parse_scene(document);
    EXPECT_EQ(empty.view().spheres, nullptr);
    EXPECT_EQ(empty.view().materials, nullptr);
}

/// More than 32 lights is valid until the light-mask AOV is requested.
TEST_F(SceneIo, ChecksLightCountOnlyForLightMask) {
    auto scene = rt::parse_scene(document);
    scene.lights.resize(32, scene.lights[0]);
    EXPECT_NO_THROW(rt::validate_scene(scene, true));
    scene.lights.push_back(scene.lights[0]);
    EXPECT_NO_THROW(rt::validate_scene(scene));
    expect_error([&] { rt::validate_scene(scene, true); }, "lights");
}

/// Generated binary references must fail explicitly until the binary reader lands.
TEST_F(SceneIo, RejectsBinaryGeometryForNow) {
    document["geometry"] = {{"binary", "scene.bin"}};
    expect_error([&] { rt::parse_scene(document); }, "geometry.binary");
}

/// The reader rejects missing files, malformed JSON, and extra trailing documents.
TEST_F(SceneIo, FileErrorsIncludeSourcePath) {
    const std::string path = (directory / "invalid.json").string();
    expect_error([&] { rt::load_scene(path); }, path);
    const std::vector<std::string> invalid = {
        "{",
        document.dump() + " {}",
        "{\"ambient\": 1e400}",
    };
    for (const auto& text : invalid) {
        SCOPED_TRACE(text);
        {
            std::ofstream output(path);
            ASSERT_TRUE(output.is_open());
            output << text;
        }
        expect_error([&] { rt::load_scene(path); }, path);
    }
}

/// Hashing ignores nonsemantic JSON metadata, the camera, and light padding.
TEST_F(SceneIo, HashExcludesCameraMetadataAndPadding) {
    const auto original = rt::parse_scene(document);
    document["camera"]["width"] = 800;
    document["camera"]["position"][0] = 1;
    document["materials"][0]["name"] = "renamed";
    auto changed = rt::parse_scene(document);
    EXPECT_EQ(original.hash, changed.hash);
    changed.lights[0].pad = 123.0f;
    EXPECT_EQ(original.hash, rt::scene_hash(changed.view()));
    changed.materials[0].albedo.x = 0.5f;
    EXPECT_NE(original.hash, rt::scene_hash(changed.view()));
    EXPECT_EQ(rt::content_hash(original.view()), rt::content_hash(changed.view()));
    changed.spheres[0].radius = 0.75f;
    EXPECT_NE(rt::content_hash(original.view()), rt::content_hash(changed.view()));
}

/// Fixed reference values were independently encoded with Python struct.pack('<I', '<Q',
/// '<f') and a byte-wise FNV-1a loop, rather than generated by the C++ hash implementation.
TEST_F(SceneIo, HashMatchesCanonicalReferenceBytes) {
    const auto scene = rt::parse_scene(document);
    EXPECT_EQ(rt::content_hash(scene.view()), 0xcfc36b235a5a3024ull);
    EXPECT_EQ(scene.hash, 0xcbed9151089614c9ull);
    const rt::Scene empty{};
    EXPECT_EQ(rt::content_hash(empty), 0xa8c7f832281a39c5ull);
}

/// Disk round trips preserve float bits (including negative zero), arrays, and the camera.
TEST_F(SceneIo, JsonRoundTripPreservesSceneFields) {
    auto scene = rt::parse_scene(document);
    scene.spheres[0].center.z = -0.0f;
    scene.spheres[1].radius = std::nextafter(0.5f, 1.0f);
    scene.materials[0].flags = rt::MAT_SPECULAR;
    scene.materials[0].ks = {0.125f, 0.25f, 0.5f};
    scene.materials[0].shininess = 12.5f;
    const auto path = (directory / "roundtrip.json").string();
    rt::save_scene(path, scene);
    const auto reloaded = rt::load_scene(path);
    EXPECT_EQ(reloaded.hash, rt::scene_hash(scene.view()));
    EXPECT_EQ(rt::scene_to_json(reloaded), rt::scene_to_json(scene));
    EXPECT_TRUE(std::signbit(reloaded.spheres[0].center.z));
    EXPECT_EQ(reloaded.spheres[1].radius, scene.spheres[1].radius);
    EXPECT_EQ(reloaded.materials[0].flags, uint32_t{rt::MAT_SPECULAR});
}

/// Saving invalid input must not truncate an existing destination.
TEST_F(SceneIo, RejectsInvalidWritesAndFileErrors) {
    auto scene = rt::parse_scene(document);
    const auto path = (directory / "saved.json").string();
    rt::save_scene(path, scene);
    scene.spheres[0].radius = -1;
    expect_error([&] { rt::save_scene(path, scene); }, "spheres[0].radius");
    EXPECT_NO_THROW(rt::load_scene(path));
    scene.spheres[0].radius = 1;
    const auto missing = (directory / "missing" / "scene.json").string();
    expect_error([&] { rt::save_scene(missing, scene); }, missing);
}

}  // namespace

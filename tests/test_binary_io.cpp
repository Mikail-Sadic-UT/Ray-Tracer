/// @file test_binary_io.cpp
/// @brief Binary layout, integrity validation, and relative JSON geometry references.
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

#include "core/error.hpp"
#include "scene/binary_io.hpp"
#include "scene/hash.hpp"
#include "scene/scene_io.hpp"

namespace {
class BinaryIo : public testing::Test {
   protected:
    void SetUp() override {
        scene = rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/known_two_spheres.json");
        directory = std::filesystem::temp_directory_path() /
                    ("rt-binary-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        created = std::filesystem::create_directory(directory);
        ASSERT_TRUE(created);
    }
    void TearDown() override {
        if (created) std::filesystem::remove_all(directory);
    }
    std::string path(const char* name) const { return (directory / name).string(); }
    static std::string bytes(const std::string& name) {
        std::ifstream input(name, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    static void store(const std::string& name, const std::string& data) {
        std::ofstream output(name, std::ios::binary);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        ASSERT_TRUE(output);
    }
    rt::SceneHost scene;
    std::filesystem::path directory;
    bool created = false;
};
}  // namespace

/// The JSON reader resolves geometry relative to its file, not the caller's working directory.
TEST_F(BinaryIo, RoundTripsGeometryAndJson) {
    rt::save_binary_scene(path("scene.json"), scene, nlohmann::json::object());
    const auto back = rt::load_scene(path("scene.json"));
    EXPECT_EQ(back.hash, scene.hash);
    EXPECT_EQ(rt::scene_to_json(back), rt::scene_to_json(scene));
    const auto geometry = rt::read_geometry(path("scene.bin"));
    EXPECT_EQ(geometry.hash, rt::content_hash(scene.view()));
    const auto data = bytes(path("scene.bin"));
    ASSERT_EQ(data.size(), 64u + 48u + 80u);
    EXPECT_EQ(data.substr(0, 8), std::string("RTSCENE\0", 8));
    EXPECT_EQ(data.substr(8, 16), std::string("\x02\0\0\0\x40\0\0\0\x02\0\0\0\x02\0\0\0", 16));
    EXPECT_EQ(data.substr(24, 16), std::string("\x40\0\0\0\0\0\0\0\x70\0\0\0\0\0\0\0", 16));
    EXPECT_EQ(data.substr(48, 16), std::string(16, '\0'));
    EXPECT_EQ(data.substr(104, 8), std::string(8, '\0'));
    EXPECT_EQ(data.substr(64, 4), std::string("\0\0\x40\xbf", 4));  // -0.75f
}

/// Odd sphere counts exercise alignment; empty geometry stays valid and hashable.
TEST_F(BinaryIo, AlignmentAndEmptyGeometry) {
    scene.spheres.resize(1);
    scene.spheres[0].center.z = -0.0f;
    rt::write_geometry(path("one.bin"), scene);
    auto back = rt::read_geometry(path("one.bin"));
    EXPECT_TRUE(std::signbit(back.spheres[0].center.z));
    EXPECT_EQ(back.hash, rt::content_hash(scene.view()));
    EXPECT_EQ(bytes(path("one.bin")).substr(84, 12), std::string(12, '\0'));
    scene.spheres.clear();
    scene.triangles.clear();
    scene.materials.clear();
    rt::save_binary_scene(path("empty.json"), scene, nlohmann::json::object());
    EXPECT_EQ(bytes(path("empty.bin")).size(), 64u);
    EXPECT_TRUE(rt::load_scene(path("empty.json")).spheres.empty());
}

/// Bad headers, huge/overlapping offsets, truncation, extra bytes, and hash corruption fail.
TEST_F(BinaryIo, RejectsMalformedFilesBeforeAllocating) {
    rt::write_geometry(path("valid.bin"), scene);
    const auto valid = bytes(path("valid.bin"));
    for (const size_t offset : {0u, 8u, 12u, 19u, 24u, 31u, 32u, 39u, 40u, 48u, 80u}) {
        SCOPED_TRACE(offset);
        auto bad = valid;
        bad[offset] ^= static_cast<char>(0xff);
        store(path("bad.bin"), bad);
        EXPECT_THROW(rt::read_geometry(path("bad.bin")), rt::Error);
    }
    for (const auto& bad : {valid.substr(0, 63), valid.substr(0, valid.size() - 1), valid + "x"}) {
        store(path("bad.bin"), bad);
        EXPECT_THROW(rt::read_geometry(path("bad.bin")), rt::Error);
    }
}

/// Binary data must still pass material and numeric validation after metadata is combined.
TEST_F(BinaryIo, ValidatesLoadedGeometryAgainstMaterials) {
    rt::save_binary_scene(path("scene.json"), scene, nlohmann::json::object());
    std::ifstream input(path("scene.json"));
    auto document = nlohmann::json::parse(input);
    document["materials"] = nlohmann::json::array();
    store(path("scene.json"), document.dump());
    EXPECT_THROW(rt::load_scene(path("scene.json")), rt::Error);
    EXPECT_THROW(rt::save_binary_scene(path("wrong.bin"), scene, nlohmann::json::object()),
                 rt::Error);
}

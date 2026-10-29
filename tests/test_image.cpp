/// @file test_image.cpp
/// @brief Image file interoperability, exact float/AOV preservation, and malformed inputs.
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include "core/error.hpp"
#include "image/image_io.hpp"

namespace {

/// Creates and removes only this fixture's temporary files.
class ImageIo : public testing::Test {
   protected:
    void SetUp() override {
        const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() / ("rt-image-" + std::to_string(id));
        created = std::filesystem::create_directory(directory);
        ASSERT_TRUE(created);
    }
    void TearDown() override {
        if (created) std::filesystem::remove_all(directory);
    }
    std::string path(const char* name) const { return (directory / name).string(); }
    static std::string bytes(const std::string& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    static void store(const std::string& path, const std::string& data) {
        std::ofstream output(path, std::ios::binary);
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        ASSERT_TRUE(output);
    }

    std::filesystem::path directory;
    bool created = false;
};

/// Disk round trips preserve unclamped samples, signed zero, NaN/Inf, and top-first ordering.
TEST_F(ImageIo, PfmPreservesFloatBits) {
    rt::Image image;
    image.width = 2;
    image.height = 2;
    image.rgb = {1,
                 2,
                 3,
                 -1,
                 -0.0f,
                 0,
                 0.125f,
                 0.25f,
                 0.5f,
                 std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity(),
                 std::numeric_limits<float>::quiet_NaN()};
    rt::write_pfm(path("image.pfm"), image);
    const auto loaded = rt::read_pfm(path("image.pfm"));
    EXPECT_EQ(loaded.width, image.width);
    EXPECT_EQ(loaded.height, image.height);
    ASSERT_EQ(loaded.rgb.size(), image.rgb.size());
    EXPECT_EQ(std::memcmp(loaded.rgb.data(), image.rgb.data(), image.rgb.size() * sizeof(float)),
              0);
    EXPECT_TRUE(loaded.prim_id.empty());
    const std::string encoded = bytes(path("image.pfm"));
    ASSERT_EQ(encoded.substr(0, 12), "PF\n2 2\n-1.0\n");
    // The first disk sample is bottom-left red = 0.125, IEEE 0x3e000000, little-endian.
    EXPECT_EQ(encoded.substr(12, 4), std::string("\x00\x00\x00\x3e", 4));
}

/// An independently encoded big-endian PFM checks scale application and row reversal.
TEST_F(ImageIo, ReadsBigEndianScaledPfm) {
    const std::string samples(
        "\x3f\x80\x00\x00\x40\x00\x00\x00\x40\x40\x00\x00"
        "\x40\x80\x00\x00\x40\xa0\x00\x00\x40\xc0\x00\x00",
        24);
    store(path("big.pfm"), "PF\r\n1 2\r\n2.0\r\n" + samples);
    const auto image = rt::read_pfm(path("big.pfm"));
    EXPECT_EQ(image.rgb, (std::vector<float>{8, 10, 12, 2, 4, 6}));
}

/// PFM payload dimensions must be checked before allocation, and extra bytes are rejected.
TEST_F(ImageIo, RejectsMalformedPfm) {
    const std::vector<std::string> bad_files = {
        "",
        "Pf\n1 1\n-1\n",
        "PF\n-1 1\n-1\n",
        "PF\n0 1\n-1\n",
        "PF\n16385 1\n-1\n",
        "PF\n1 1 extra\n-1\n",
        "PF\n1 1\n0\n",
        "PF\n1 1\nnan\n",
        "PF\n1 1\n-1 extra\n",
        "PF\n1 1\n-1\n",
        "PF\n16384 16384\n-1\n",
    };
    for (const auto& data : bad_files) {
        SCOPED_TRACE(data);
        store(path("bad.pfm"), data);
        EXPECT_THROW(rt::read_pfm(path("bad.pfm")), rt::Error);
    }
    store(path("bad.pfm"), "PF\n1 1\n-1\n" + std::string(13, '\0'));
    EXPECT_THROW(rt::read_pfm(path("bad.pfm")), rt::Error);
    EXPECT_THROW(rt::read_pfm(path("missing.pfm")), rt::Error);
}

/// All uint32 bits, including miss sentinels and the top primitive tag bit, survive IO.
TEST_F(ImageIo, U32ImageMatchesCanonicalBytes) {
    const std::vector<uint32_t> pixels = {0xffffffffu, 0x80000001u, 0x12345678u, 0u};
    rt::write_u32img(path("ids.u32img"), 2, 2, pixels);
    const auto loaded = rt::read_u32img(path("ids.u32img"));
    EXPECT_EQ(loaded.width, 2u);
    EXPECT_EQ(loaded.height, 2u);
    EXPECT_EQ(loaded.pixels, pixels);
    std::string expected = "RTU32IMG";
    expected += std::string("\x01\0\0\0\x02\0\0\0\x02\0\0\0\x01\0\0\0", 16);
    expected += std::string(8, '\0');
    expected += std::string("\xff\xff\xff\xff\x01\0\0\x80\x78\x56\x34\x12\0\0\0\0", 16);
    EXPECT_EQ(bytes(path("ids.u32img")), expected);
}

/// Malformed integer-image headers, truncated payloads, and trailing bytes cannot load.
TEST_F(ImageIo, RejectsMalformedU32Images) {
    rt::write_u32img(path("valid.u32img"), 1, 1, {0xffffffffu});
    const std::string valid = bytes(path("valid.u32img"));
    for (const size_t offset : {0u, 8u, 12u, 16u, 20u, 24u, 31u}) {
        SCOPED_TRACE(offset);
        auto corrupted = valid;
        corrupted[offset] = 0x7f;
        store(path("bad.u32img"), corrupted);
        EXPECT_THROW(rt::read_u32img(path("bad.u32img")), rt::Error);
    }
    for (const auto& corrupted : {valid.substr(0, 20), valid.substr(0, 35), valid + "x"}) {
        store(path("bad.u32img"), corrupted);
        EXPECT_THROW(rt::read_u32img(path("bad.u32img")), rt::Error);
    }
    EXPECT_THROW(rt::read_u32img(path("missing.u32img")), rt::Error);
}

/// Decoding PNG through a separate reader checks sRGB encoding, clamping, and row order.
TEST_F(ImageIo, PngEncodesSrgbAndClampsOnlyOutput) {
    rt::Image image;
    image.width = 1;
    image.height = 2;
    image.rgb = {-1, 0.0031308f, 0.5f, 1, 2, 0};
    const auto original = image.rgb;
    rt::write_png(path("image.png"), image);
    int width = 0, height = 0, channels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
        stbi_load(path("image.png").c_str(), &width, &height, &channels, 3), stbi_image_free);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(width, 1);
    EXPECT_EQ(height, 2);
    EXPECT_EQ(channels, 3);
    const std::vector<unsigned char> expected = {0, 10, 188, 255, 255, 0};
    EXPECT_EQ(std::vector<unsigned char>(decoded.get(), decoded.get() + 6), expected);
    EXPECT_EQ(image.rgb, original);
}

/// Sidecars follow the PFM stem and preserve only the AOVs requested by the caller.
TEST_F(ImageIo, WritesOnlyRequestedAovSidecars) {
    rt::Image image;
    image.width = 2;
    image.height = 1;
    image.prim_id = {0xffffffffu, 0x80000001u};
    rt::write_aovs(path("frame.pfm"), image);
    EXPECT_EQ(rt::read_u32img(path("frame.primid.u32img")).pixels, image.prim_id);
    EXPECT_FALSE(std::filesystem::exists(path("frame.lightmask.u32img")));
    image.light_mask = {0, 0x80000000u};
    rt::write_aovs(path("frame.pfm"), image);
    EXPECT_EQ(rt::read_u32img(path("frame.lightmask.u32img")).pixels, image.light_mask);
    EXPECT_FALSE(std::filesystem::exists(path("frame.pfm")));
}

/// Writers check dimensions, data lengths, finite PNG input, and output failures.
TEST_F(ImageIo, RejectsInvalidWrites) {
    rt::Image image;
    EXPECT_THROW(rt::write_pfm(path("bad.pfm"), image), rt::Error);
    EXPECT_THROW(rt::write_png(path("bad.png"), image), rt::Error);
    image.width = image.height = 1;
    image.rgb = {0, 0};
    EXPECT_THROW(rt::write_pfm(path("bad.pfm"), image), rt::Error);
    EXPECT_THROW(rt::write_png(path("bad.png"), image), rt::Error);
    image.rgb = {0, 0, std::numeric_limits<float>::quiet_NaN()};
    EXPECT_THROW(rt::write_png(path("bad.png"), image), rt::Error);
    image.rgb = {0, 0, 0};
    EXPECT_THROW(rt::write_pfm(path("missing/bad.pfm"), image), rt::Error);
    EXPECT_THROW(rt::write_png(path("missing/bad.png"), image), rt::Error);
    EXPECT_THROW(rt::write_u32img(path("bad.u32img"), 1, 1, {}), rt::Error);
    EXPECT_THROW(rt::write_u32img(path("bad.u32img"), 16385, 1, {}), rt::Error);
    EXPECT_THROW(rt::write_u32img(path("missing/bad.u32img"), 1, 1, {0}), rt::Error);
    image.light_mask = {0, 1};
    EXPECT_THROW(rt::write_aovs(path("bad.pfm"), image), rt::Error);
}

}  // namespace

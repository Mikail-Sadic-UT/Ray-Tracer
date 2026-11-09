/// @file test_compare.cpp
/// @brief Core corruption checks on the known normal image and synthetic shadow inputs.
#include <gtest/gtest.h>

#include <limits>

#include "core/error.hpp"
#include "cpu/normal_renderer.hpp"
#include "image/compare.hpp"
#include "scene/scene_io.hpp"

namespace {
class Compare : public testing::Test {
   protected:
    static void SetUpTestSuite() {
        const auto scene =
            rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/known_two_spheres.json");
        rt::Timings timings;
        rt::render_normal(scene, reference, {2, true, true}, timings);
    }
    static rt::Image reference;
};
rt::Image Compare::reference;
}  // namespace

/// Identical images pass every check, while NaN in either input fails the nonfinite check.
TEST_F(Compare, IdenticalAndNonfinite) {
    EXPECT_TRUE(rt::compare_images(reference, reference).passed());
    auto changed = reference;
    changed.rgb[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(rt::compare_images(reference, changed).nonfinite, 1u);
    EXPECT_FALSE(rt::compare_images(changed, reference).passed());
}

/// Moving sphere zero three pixels produces ID changes that a 3x3 neighborhood cannot explain.
TEST_F(Compare, RejectsThreePixelSphereShift) {
    auto changed = reference;
    size_t copied = 0;
    for (uint32_t y = 0; y < reference.height; ++y) {
        for (uint32_t x = 0; x + 3 < reference.width; ++x) {
            const size_t from = static_cast<size_t>(y) * reference.width + x;
            if (reference.prim_id[from] != 0u) continue;
            const size_t to = from + 3;
            for (unsigned c = 0; c < 3; ++c) changed.rgb[3 * to + c] = reference.rgb[3 * from + c];
            changed.prim_id[to] = reference.prim_id[from];
            changed.light_mask[to] = reference.light_mask[from];
            ++copied;
        }
    }
    ASSERT_GT(copied, 1600u);  // fixture is much larger than a 20-pixel-radius sphere
    EXPECT_GT(rt::compare_images(reference, changed).unexplained_hits, 0u);
}

/// A 10x10 corruption well inside sphere zero must fail linear shading agreement.
TEST_F(Compare, RejectsInteriorColorBlock) {
    auto changed = reference;
    for (uint32_t y = 224; y < 254; ++y)
        for (uint32_t x = 217; x < 247; ++x)
            ASSERT_EQ(reference.prim_id[static_cast<size_t>(y) * reference.width + x], 0u);
    for (uint32_t y = 234; y < 244; ++y)
        for (uint32_t x = 227; x < 237; ++x)
            for (size_t c = 0; c < 3; ++c)
                changed.rgb[3 * (static_cast<size_t>(y) * reference.width + x) + c] += 0.1f;
    EXPECT_EQ(rt::compare_images(reference, changed).shading_mismatches, 100u);
}

// STUB(S5, po): remove when LitShading lands and corruption test 4 uses a real lit image (#43).
/// Synthetic masks model a uniformly lit floor region until real Lambert reference images exist.
TEST_F(Compare, RejectsSyntheticShadowBlock) {
    auto lit = reference;
    for (size_t i = 0; i < lit.prim_id.size(); ++i)
        if ((lit.prim_id[i] & 0x80000000u) && lit.prim_id[i] != 0xffffffffu) lit.light_mask[i] = 1;
    for (uint32_t y = 390; y < 430; ++y)
        for (uint32_t x = 290; x < 330; ++x)
            ASSERT_EQ(lit.light_mask[static_cast<size_t>(y) * lit.width + x], 1u);
    auto changed = lit;
    for (uint32_t y = 400; y < 420; ++y)
        for (uint32_t x = 300; x < 320; ++x) {
            const size_t i = static_cast<size_t>(y) * lit.width + x;
            changed.light_mask[i] = 0;
            for (size_t c = 0; c < 3; ++c) changed.rgb[i * 3 + c] = 0.035f;
        }
    EXPECT_EQ(rt::compare_images(lit, changed).unexplained_shadows, 400u);
}

/// Plausible local swaps need both hit explanations and must remain within the pixel budget.
TEST(CompareRules, SymmetricHitExplanationsAndBudget) {
    rt::Image image;
    image.width = 4;
    image.height = 1;
    image.rgb.resize(12, 0.5f);
    image.prim_id = {0, 1, 0, 1};
    image.light_mask.resize(4, 0);
    auto changed = image;
    changed.prim_id = {1, 0, 1, 0};
    auto result = rt::compare_images(image, changed);
    EXPECT_EQ(result.unexplained_hits, 0u);
    EXPECT_EQ(result.explained_mismatches, 4u);
    EXPECT_FALSE(result.budget_pass);
    EXPECT_TRUE(rt::compare_images(image, changed, {1e-5, 1e-4, 1}).passed());
    changed.prim_id = {1, 1, 1, 1};
    EXPECT_GT(rt::compare_images(image, changed).unexplained_hits, 0u);
    image.prim_id = {0, 0, 0, 1};
    changed = image;
    changed.prim_id[0] = 1;
    EXPECT_EQ(rt::compare_images(image, changed).unexplained_hits, 1u);  // no row wrap
}

/// Tolerances use unclamped floats; matching sRGB does not waive a linear mismatch.
TEST(CompareRules, LinearToleranceAndInvalidInputs) {
    rt::Image image;
    image.width = image.height = 1;
    image.rgb = {2, 2, 2};
    image.prim_id = {0};
    image.light_mask = {0};
    auto changed = image;
    changed.rgb[0] = 3;
    const auto result = rt::compare_images(image, changed);
    EXPECT_EQ(result.shading_mismatches, 1u);
    EXPECT_EQ(result.srgb_close, 1u);
    EXPECT_THROW(rt::compare_images(image, changed, {-1, 0, 0}), rt::Error);
    changed.prim_id.clear();
    EXPECT_THROW(rt::compare_images(image, changed), rt::Error);
}

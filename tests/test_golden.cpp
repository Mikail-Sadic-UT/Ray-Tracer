/// @file test_golden.cpp
/// @brief Lit regression goldens for the known scenes: renders each scene through CpuBackend
///        and compares it with the committed golden using the normal rt_compare tolerances.
///
/// These goldens are REGRESSION-ONLY. They were rendered by this repository's own renderer
/// (see scenes/golden/README.md), so they catch a change in the lit output, not an error that
/// was already there. They do not validate shading independently: the analytic and independent
/// shading values live in test_shade and test_shade_independent.
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "bvh/bvh.hpp"
#include "cpu/cpu_backend.hpp"
#include "image/compare.hpp"
#include "image/image.hpp"
#include "image/image_io.hpp"
#include "scene/scene_io.hpp"
#include "trace/hit.hpp"

namespace {

struct Expect {
    const char* name;
    std::vector<rt::PrimRef> ids;  ///< Primary-hit IDs the golden must contain (misses excluded).
};

/// What each golden must contain, read from the scene files: sphere indices are 0 and 1,
/// triangle IDs carry rt::PRIM_TRIANGLE_BIT. The degenerate scene's third triangle (index 2)
/// has zero area and must never be visible.
const Expect kScenes[] = {
    {"known_two_spheres", {0, 1, rt::PRIM_TRIANGLE_BIT | 0, rt::PRIM_TRIANGLE_BIT | 1}},
    {"known_single_sphere", {0, rt::PRIM_TRIANGLE_BIT | 0, rt::PRIM_TRIANGLE_BIT | 1}},
    {"known_degenerate_triangle", {0, 1, rt::PRIM_TRIANGLE_BIT | 0, rt::PRIM_TRIANGLE_BIT | 1}},
};

std::string golden_path(const std::string& name, const char* suffix) {
    return std::string(RT_GOLDEN_DIR) + "/" + name + suffix;
}

/// Loads a golden: linear RGB from the PFM plus both AOVs.
rt::Image load_golden(const std::string& name) {
    rt::Image image = rt::read_pfm(golden_path(name, ".pfm"));
    const rt::U32Image ids = rt::read_u32img(golden_path(name, ".primid.u32img"));
    const rt::U32Image masks = rt::read_u32img(golden_path(name, ".lightmask.u32img"));
    EXPECT_EQ(ids.width, image.width);
    EXPECT_EQ(ids.height, image.height);
    EXPECT_EQ(masks.width, image.width);
    EXPECT_EQ(masks.height, image.height);
    image.prim_id = ids.pixels;
    image.light_mask = masks.pixels;
    return image;
}

/// Renders @p scene lit with both AOVs. @p bvh null means brute force.
rt::Image render_lit(const rt::SceneHost& scene, const rt::Bvh* bvh, int threads) {
    rt::CpuBackend backend;
    rt::Timings timings;
    backend.prepare({&scene,
                     bvh,
                     {scene.camera.width, scene.camera.height, rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK,
                      rt::ShadeMode::Lit},
                     1,
                     threads,
                     {}},
                    timings);
    rt::Image image;
    backend.render(scene.camera, image, timings);
    return image;
}

rt::Bvh median_bvh(const rt::SceneHost& scene) {
    rt::BvhParams params;
    rt::Timings timings;
    return rt::build_bvh(scene.view(), params, timings);
}

}  // namespace

/// Each golden has the scene's resolution, only finite values, and the primitives the scene
/// file says are visible (and, for the degenerate scene, not the zero-area triangle). Both lit
/// and shadowed floor pixels exist, so the goldens exercise shadow rays.
TEST(Golden, FilesDescribeTheirScene) {
    for (const Expect& expect : kScenes) {
        SCOPED_TRACE(expect.name);
        const rt::SceneHost scene =
            rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/" + expect.name + ".json");
        const rt::Image golden = load_golden(expect.name);
        ASSERT_EQ(golden.width, scene.camera.width);
        ASSERT_EQ(golden.height, scene.camera.height);
        ASSERT_EQ(golden.rgb.size(), 3u * golden.width * golden.height);
        for (const float value : golden.rgb) ASSERT_TRUE(std::isfinite(value));
        std::set<rt::PrimRef> seen(golden.prim_id.begin(), golden.prim_id.end());
        EXPECT_TRUE(seen.erase(rt::RT_MISS_PRIM)) << "no background pixels";
        EXPECT_EQ(seen, std::set<rt::PrimRef>(expect.ids.begin(), expect.ids.end()));
        size_t lit = 0, shadowed = 0;
        for (size_t i = 0; i < golden.prim_id.size(); ++i) {
            if (golden.prim_id[i] == rt::RT_MISS_PRIM) continue;
            (golden.light_mask[i] != 0 ? lit : shadowed)++;
        }
        EXPECT_GT(lit, 10000u);
        EXPECT_GT(shadowed, 1000u);
    }
}

/// A lit render matches the golden within the rt_compare tolerances: with the median BVH on 1
/// and 4 threads, and by brute force on 4 threads.
TEST(Golden, LitRenderMatches) {
    for (const Expect& expect : kScenes) {
        SCOPED_TRACE(expect.name);
        const rt::SceneHost scene =
            rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/" + expect.name + ".json");
        const rt::Image golden = load_golden(expect.name);
        const rt::Bvh bvh = median_bvh(scene);
        const struct {
            const char* label;
            const rt::Bvh* bvh;
            int threads;
        } configs[] = {{"median BVH, 1 thread", &bvh, 1},
                       {"median BVH, 4 threads", &bvh, 4},
                       {"brute force, 4 threads", nullptr, 4}};
        for (const auto& config : configs) {
            SCOPED_TRACE(config.label);
            const rt::CompareResult result =
                rt::compare_images(golden, render_lit(scene, config.bvh, config.threads));
            EXPECT_TRUE(result.passed());
            EXPECT_EQ(result.nonfinite, 0u);
            EXPECT_EQ(result.unexplained_hits, 0u);
            EXPECT_EQ(result.unexplained_shadows, 0u);
            EXPECT_EQ(result.shading_mismatches, 0u);
        }
    }
}

/// The comparison is armed: a render with a visible change fails against the golden. One hit
/// pixel away from any edge gets +0.01 in green (a shading change far above the tolerance), and
/// another has its light bit flipped (a shadow change no neighbor explains).
TEST(Golden, ChangedGoldenIsRejected) {
    const rt::SceneHost scene =
        rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/known_two_spheres.json");
    const rt::Image golden = load_golden("known_two_spheres");
    const rt::Bvh bvh = median_bvh(scene);
    const rt::Image rendered = render_lit(scene, &bvh, 2);
    ASSERT_TRUE(rt::compare_images(golden, rendered).passed());

    // The first pixel whose 3x3 neighborhood has one ID and one mask (nowhere near an edge).
    const auto interior = [&](size_t skip) {
        size_t found = 0;
        for (uint32_t y = 2; y + 2 < golden.height; ++y) {
            for (uint32_t x = 2; x + 2 < golden.width; ++x) {
                const size_t i = size_t(y) * golden.width + x;
                if (golden.prim_id[i] == rt::RT_MISS_PRIM) continue;
                bool uniform = true;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const size_t j = static_cast<size_t>(
                            static_cast<std::ptrdiff_t>(i) +
                            dy * static_cast<std::ptrdiff_t>(golden.width) + dx);
                        uniform = uniform && golden.prim_id[j] == golden.prim_id[i] &&
                                  golden.light_mask[j] == golden.light_mask[i];
                    }
                }
                if (uniform && found++ == skip) return i;
            }
        }
        return size_t(-1);
    };
    const size_t shaded = interior(1000), flipped = interior(5000);
    ASSERT_NE(shaded, size_t(-1));
    ASSERT_NE(flipped, size_t(-1));

    // The corruption goes into the test image, as in test_compare: a shadow mismatch is
    // explained only if the test's mask appears next to the same pixel in the reference.
    rt::Image shading_changed = rendered;
    shading_changed.rgb[3 * shaded + 1] += 0.01f;
    const rt::CompareResult shading = rt::compare_images(golden, shading_changed);
    EXPECT_FALSE(shading.passed());
    EXPECT_GE(shading.shading_mismatches, 1u);

    rt::Image mask_changed = rendered;
    mask_changed.light_mask[flipped] ^= 1u;
    const rt::CompareResult mask = rt::compare_images(golden, mask_changed);
    EXPECT_FALSE(mask.passed());
    EXPECT_GE(mask.unexplained_shadows, 1u);
}

/// @file test_backend.cpp
/// @brief CpuBackend lifecycle, query dispatch, determinism, and error reporting.
// cpu_backend.hpp first, so this file checks that it and backend.hpp compile on their own.
#include "cpu/cpu_backend.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "bvh/bvh.hpp"
#include "core/error.hpp"
#include "cpu/normal_renderer.hpp"
#include "scene/generator.hpp"
#include "scene/scene_io.hpp"
#include "shade/render_pixel.hpp"
#include "trace/brute.hpp"
#include "trace/hit.hpp"

namespace {

rt::SceneHost known_scene(const char* name = "known_two_spheres") {
    return rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/" + name + ".json");
}

/// Uniform generated scene (spheres plus a terrain grid with shared edges), at a small
/// resolution so brute force stays fast.
rt::SceneHost generated_scene(uint32_t primitives, uint32_t width, uint32_t height) {
    rt::UniformSettings g;
    g.primitives = primitives;
    g.seed = 7;
    rt::SceneHost s = rt::generate_uniform(g);
    s.camera.width = width;
    s.camera.height = height;
    return s;
}

rt::Bvh median_bvh(const rt::SceneHost& s, uint32_t leaf = 4) {
    rt::BvhParams p;
    p.max_leaf_size = leaf;
    rt::Timings t;
    return rt::build_bvh(s.view(), p, t);
}

rt::PrepareInput input(const rt::SceneHost& s, const rt::Bvh* bvh, int threads = 1,
                       uint32_t aovs = rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK,
                       rt::ShadeMode shade = rt::ShadeMode::Normal) {
    return {&s, bvh, {s.camera.width, s.camera.height, aovs, shade}, 1, threads, {}};
}

/// Renders one frame of @p s through a fresh CpuBackend.
rt::Image render(const rt::SceneHost& s, const rt::Bvh* bvh, int threads,
                 uint32_t aovs = rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK,
                 rt::ShadeMode shade = rt::ShadeMode::Normal) {
    rt::CpuBackend b;
    rt::Timings t;
    b.prepare(input(s, bvh, threads, aovs, shade), t);
    rt::Image img;
    b.render(s.camera, img, t);
    EXPECT_GE(t.render, 0.0);
    b.release();
    return img;
}

/// Expects identical dimensions and bytes in RGB and both AOVs.
void expect_identical(const rt::Image& a, const rt::Image& b) {
    ASSERT_EQ(a.width, b.width);
    ASSERT_EQ(a.height, b.height);
    ASSERT_EQ(a.rgb.size(), b.rgb.size());
    EXPECT_EQ(std::memcmp(a.rgb.data(), b.rgb.data(), a.rgb.size() * sizeof(float)), 0);
    EXPECT_EQ(a.prim_id, b.prim_id);
    EXPECT_EQ(a.light_mask, b.light_mask);
}

/// Expects @p f to throw rt::Error whose message contains @p text.
/// Counts hit pixels with no contributing light (in shadow from every light).
size_t shadowed_hits(const rt::Image& img) {
    size_t n = 0;
    for (size_t i = 0; i < img.prim_id.size(); ++i)
        n += img.prim_id[i] != rt::RT_MISS_PRIM && img.light_mask[i] == 0;
    return n;
}

template <class F>
void expect_error(F f, const std::string& text) {
    try {
        f();
        ADD_FAILURE() << "no rt::Error thrown; expected one containing '" << text << "'";
    } catch (const rt::Error& e) {
        EXPECT_NE(std::string(e.what()).find(text), std::string::npos) << e.what();
    }
}

TEST(CpuBackend, NameAndNeedsBvh) {
    rt::CpuBackend b;
    EXPECT_STREQ(b.name(), "cpu");
    EXPECT_TRUE(b.needs_bvh());
}

/// Brute force and the BVH through the backend both reproduce render_normal byte for byte, so
/// rt_render --backend cpu replaces scene_dump --render-normal (stub S4) exactly.
TEST(CpuBackend, MatchesRenderNormal) {
    const rt::SceneHost s = known_scene();
    rt::Image reference;
    rt::Timings t;
    rt::render_normal(s, reference, {1, true, true}, t);
    const rt::Bvh bvh = median_bvh(s, 1);
    expect_identical(render(s, nullptr, 1), reference);
    expect_identical(render(s, &bvh, 3), reference);
}

/// On a generated scene, BVH and brute-force images are byte-identical for every leaf size and
/// thread count, and the image actually contains hits on several primitives.
TEST(CpuBackend, BvhEqualsBruteForceForAnyThreadCount) {
    const rt::SceneHost s = generated_scene(3000, 96, 64);
    const rt::Image brute = render(s, nullptr, 1);
    size_t hits = 0;
    for (uint32_t p : brute.prim_id) hits += p != rt::RT_MISS_PRIM;
    EXPECT_GT(hits, brute.prim_id.size() / 4);
    for (uint32_t leaf : {1u, 4u, 8u}) {
        const rt::Bvh bvh = median_bvh(s, leaf);
        for (int threads : {1, 5, 16}) {
            SCOPED_TRACE("leaf " + std::to_string(leaf) + " threads " + std::to_string(threads));
            expect_identical(render(s, &bvh, threads), brute);
        }
    }
}

/// `--shade lit` gives exactly what render_pixel<BruteQuery, LitShading> gives for every pixel,
/// and the known scene has both lit and fully shadowed hits, so shadow rays are exercised.
TEST(CpuBackend, LitMatchesRenderPixel) {
    const rt::SceneHost s = known_scene();
    const rt::Bvh bvh = median_bvh(s, 1);
    const rt::Image brute =
        render(s, nullptr, 1, rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK, rt::ShadeMode::Lit);
    const rt::Scene view = s.view();
    const rt::BruteQuery q{view};
    size_t lit = 0;
    for (uint32_t y = 0; y < s.camera.height; ++y) {
        for (uint32_t x = 0; x < s.camera.width; ++x) {
            const rt::PixelResult p =
                rt::render_pixel<rt::BruteQuery, rt::LitShading>(s.camera, x, y, view, q);
            const size_t i = size_t(y) * s.camera.width + x;
            ASSERT_EQ(brute.rgb[3 * i], p.rgb.x);
            ASSERT_EQ(brute.rgb[3 * i + 1], p.rgb.y);
            ASSERT_EQ(brute.rgb[3 * i + 2], p.rgb.z);
            ASSERT_EQ(brute.prim_id[i], p.prim_id);
            ASSERT_EQ(brute.light_mask[i], p.light_mask);
            lit += p.light_mask != 0;
        }
    }
    EXPECT_GT(lit, 0u);
    EXPECT_GT(shadowed_hits(brute), 0u);
    expect_identical(render(s, &bvh, 3, rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK, rt::ShadeMode::Lit),
                     brute);
    // Lit and normal shading are different images of the same hits.
    const rt::Image normal = render(s, nullptr, 1);
    EXPECT_EQ(normal.prim_id, brute.prim_id);
    EXPECT_NE(std::memcmp(normal.rgb.data(), brute.rgb.data(), normal.rgb.size() * sizeof(float)),
              0);
}

/// Lit images, including every shadow ray, are byte-identical between the BVH and brute force
/// for every leaf size and thread count.
TEST(CpuBackend, LitBvhEqualsBruteForceForAnyThreadCount) {
    const rt::SceneHost s = generated_scene(3000, 96, 64);
    const uint32_t aovs = rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK;
    const rt::Image brute = render(s, nullptr, 1, aovs, rt::ShadeMode::Lit);
    EXPECT_GT(shadowed_hits(brute), 0u);
    for (uint32_t leaf : {1u, 4u}) {
        const rt::Bvh bvh = median_bvh(s, leaf);
        for (int threads : {1, 5, 16}) {
            SCOPED_TRACE("leaf " + std::to_string(leaf) + " threads " + std::to_string(threads));
            expect_identical(render(s, &bvh, threads, aovs, rt::ShadeMode::Lit), brute);
        }
    }
}

/// Only requested AOVs are produced; a reused Image has unrequested AOVs cleared.
TEST(CpuBackend, AovsFollowSettings) {
    const rt::SceneHost s = known_scene();
    rt::CpuBackend b;
    rt::Timings t;
    rt::Image img;
    b.prepare(input(s, nullptr, 2), t);
    b.render(s.camera, img, t);
    EXPECT_EQ(img.prim_id.size(), size_t(s.camera.width) * s.camera.height);
    EXPECT_EQ(img.light_mask.size(), img.prim_id.size());
    b.prepare(input(s, nullptr, 2, rt::AOV_PRIM_ID), t);
    b.render(s.camera, img, t);
    EXPECT_EQ(img.prim_id.size(), size_t(s.camera.width) * s.camera.height);
    EXPECT_TRUE(img.light_mask.empty());
    b.prepare(input(s, nullptr, 2, 0), t);
    b.render(s.camera, img, t);
    EXPECT_TRUE(img.prim_id.empty());
    EXPECT_EQ(img.rgb.size(), 3 * size_t(s.camera.width) * s.camera.height);
}

/// render() is repeatable, the camera may move between frames at the same resolution, and
/// prepare() may be called again; release() is idempotent.
TEST(CpuBackend, Lifecycle) {
    const rt::SceneHost s = known_scene();
    const rt::Bvh bvh = median_bvh(s);
    rt::CpuBackend b;
    rt::Timings t;
    b.prepare(input(s, &bvh, 2), t);
    rt::Image first, second, moved;
    b.render(s.camera, first, t);
    b.render(s.camera, second, t);
    expect_identical(first, second);
    rt::Camera cam = s.camera;
    cam.position = cam.position + rt::vec3{0.5f, 0.0f, 0.0f};
    b.render(cam, moved, t);
    EXPECT_NE(std::memcmp(first.rgb.data(), moved.rgb.data(), first.rgb.size() * sizeof(float)), 0);
    b.prepare(input(s, nullptr, 1), t);  // re-prepare with brute force
    b.render(s.camera, second, t);
    expect_identical(first, second);
    b.release();
    b.release();
    expect_error([&] { b.render(s.camera, second, t); }, "before prepare");
}

/// An empty scene renders the background with an empty BVH and with brute force.
TEST(CpuBackend, EmptyScene) {
    rt::SceneHost s = known_scene();
    s.spheres.clear();
    s.triangles.clear();
    const rt::Bvh bvh = median_bvh(s);
    ASSERT_TRUE(bvh.nodes.empty());
    const rt::Image a = render(s, &bvh, 2);
    expect_identical(a, render(s, nullptr, 1));
    for (uint32_t p : a.prim_id) ASSERT_EQ(p, rt::RT_MISS_PRIM);
    EXPECT_EQ(a.rgb[0], s.background.x);
}

TEST(CpuBackend, RejectsInvalidInput) {
    const rt::SceneHost s = known_scene();
    rt::CpuBackend b;
    rt::Timings t;
    rt::Image img;
    expect_error([&] { b.render(s.camera, img, t); }, "before prepare");

    rt::PrepareInput in = input(s, nullptr);
    in.scene = nullptr;
    expect_error([&] { b.prepare(in, t); }, "scene is null");
    in = input(s, nullptr, 0);
    expect_error([&] { b.prepare(in, t); }, "thread count");
    in = input(s, nullptr);
    in.render.width = 0;
    expect_error([&] { b.prepare(in, t); }, "width and height");
    in = input(s, nullptr, 1, 1u << 5);
    expect_error([&] { b.prepare(in, t); }, "unknown AOV");
    in = input(s, nullptr, 1, rt::AOV_PRIM_ID, static_cast<rt::ShadeMode>(7));
    expect_error([&] { b.prepare(in, t); }, "unknown shade mode");

    rt::SceneHost many_lights = s;
    many_lights.lights.assign(33, s.lights.empty() ? rt::Light{} : s.lights[0]);
    expect_error([&] { b.prepare(input(many_lights, nullptr), t); }, "at most 32 lights");
    EXPECT_NO_THROW(b.prepare(input(many_lights, nullptr, 1, rt::AOV_PRIM_ID), t));

    rt::SceneHost other = s;
    other.spheres.pop_back();
    const rt::Bvh wrong = median_bvh(other);
    expect_error([&] { b.prepare(input(s, &wrong), t); }, "primitive references");

    // A failed prepare leaves the backend unprepared.
    expect_error([&] { b.render(s.camera, img, t); }, "before prepare");
}

/// The camera must have the prepared resolution; a change needs a new prepare().
TEST(CpuBackend, RejectsResolutionChange) {
    const rt::SceneHost s = known_scene();
    rt::CpuBackend b;
    rt::Timings t;
    b.prepare(input(s, nullptr), t);
    rt::Camera cam = s.camera;
    cam.width += 1;
    rt::Image img;
    expect_error([&] { b.render(cam, img, t); }, "differs from the prepared");
}

#ifdef NDEBUG
/// A BVH deeper than 64 levels makes render() fail through the traversal overflow flag
/// instead of returning a silently wrong image. (Debug builds assert inside traversal.)
TEST(CpuBackend, StackOverflowFailsRender) {
    rt::SceneHost s = known_scene();
    s.camera.width = 8;
    s.camera.height = 8;
    s.triangles.clear();
    s.spheres.resize(1);
    const rt::Sphere& sp = s.spheres[0];
    const rt::vec3 r{sp.radius, sp.radius, sp.radius};
    const rt::AABB box{sp.center - r, sp.center + r};
    rt::Bvh bvh;  // chain: left child interior, right child a leaf; one push per level
    bvh.prim_refs = {0u};
    bvh.nodes.push_back({box, 0u, 0u});
    uint32_t cur = 0;
    for (int k = 0; k < rt::RT_MAX_BVH_DEPTH + 1; ++k) {
        const uint32_t left = static_cast<uint32_t>(bvh.nodes.size());
        bvh.nodes[cur].left_or_first = left;
        bvh.nodes.push_back({box, 0u, 0u});
        bvh.nodes.push_back({box, 0u, 1u});
        cur = left;
    }
    bvh.nodes[cur].count = 1u;
    // Aim every pixel at the sphere so each ray enters the chain.
    s.camera.look_at = sp.center;
    s.camera.vfov_deg = 1.0f;
    rt::CpuBackend b;
    rt::Timings t;
    b.prepare(input(s, &bvh, 2), t);
    rt::Image img;
    expect_error([&] { b.render(s.camera, img, t); }, "stack overflow");
}
#endif

}  // namespace

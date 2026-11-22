/// @file test_gpu.cpp
/// @brief CudaBackend and DeviceScene: lifecycle, timings, determinism, error reporting, and
/// agreement with CpuBackend under rt_compare (ROADMAP C10).
///
/// Built only with CUDA. Every test that touches the device starts with
/// RT_SKIP_WITHOUT_CUDA_DEVICE, so a machine with the toolkit but no GPU skips them.
// cuda_backend.hpp first, so this file checks that it compiles as plain C++ on its own.
#include "gpu/cuda_backend.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "bvh/bvh.hpp"
#include "core/error.hpp"
#include "cpu/cpu_backend.hpp"
#include "cuda_test_util.hpp"
#include "gpu/device_scene.hpp"
#include "image/compare.hpp"
#include "scene/generator.hpp"
#include "scene/scene_io.hpp"
#include "trace/hit.hpp"
namespace {
rt::SceneHost known_scene(const std::string& name = "known_two_spheres") {
    return rt::load_scene(std::string(RT_TEST_SCENES_DIR) + "/" + name + ".json");
}
/// Uniform generated scene (spheres plus a terrain grid with shared edges) at a small
/// resolution, so the CPU reference stays fast.
rt::SceneHost generated_scene(uint32_t primitives, uint32_t width, uint32_t height) {
    rt::UniformSettings g;
    g.primitives = primitives;
    g.seed = 7;
    rt::SceneHost s = rt::generate_uniform(g);
    s.camera.width = width;
    s.camera.height = height;
    return s;
}
rt::Bvh build(const rt::SceneHost& s, rt::BvhMethod method = rt::BvhMethod::Median) {
    rt::BvhParams p;
    p.method = method;
    p.threads = 4;
    rt::Timings t;
    return rt::build_bvh(s.view(), p, t);
}
constexpr uint32_t kAllAovs = rt::AOV_PRIM_ID | rt::AOV_LIGHT_MASK;
rt::PrepareInput input(const rt::SceneHost& s, const rt::Bvh* bvh, uint32_t aovs = kAllAovs,
                       rt::ShadeMode shade = rt::ShadeMode::Lit) {
    return {&s, bvh, {s.camera.width, s.camera.height, aovs, shade}, 1, 8, {}};
}
/// Renders one frame of @p s through a fresh backend of type B.
template <class B>
rt::Image render(const rt::SceneHost& s, const rt::Bvh* bvh, uint32_t aovs = kAllAovs,
                 rt::ShadeMode shade = rt::ShadeMode::Lit) {
    B b;
    rt::Timings t;
    b.prepare(input(s, bvh, aovs, shade), t);
    rt::Image img;
    b.render(s.camera, img, t);
    b.release();
    return img;
}
void expect_identical(const rt::Image& a, const rt::Image& b) {
    ASSERT_EQ(a.width, b.width);
    ASSERT_EQ(a.height, b.height);
    ASSERT_EQ(a.rgb.size(), b.rgb.size());
    EXPECT_EQ(std::memcmp(a.rgb.data(), b.rgb.data(), a.rgb.size() * sizeof(float)), 0);
    EXPECT_EQ(a.prim_id, b.prim_id);
    EXPECT_EQ(a.light_mask, b.light_mask);
}
/// Expects the CUDA image to pass rt_compare against the CPU reference with the default
/// (BENCHMARKING "Quality protocol") tolerances.
void expect_matches_cpu(const rt::Image& cpu, const rt::Image& gpu) {
    const rt::CompareResult r = rt::compare_images(cpu, gpu);
    EXPECT_TRUE(r.passed()) << "nonfinite " << r.nonfinite << ", unexplained hits "
                            << r.unexplained_hits << ", unexplained shadows "
                            << r.unexplained_shadows << ", explained " << r.explained_mismatches
                            << ", shading " << r.shading_mismatches;
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

TEST(CudaBackend, NameAndNeedsBvh) {
    rt::CudaBackend b;
    EXPECT_STREQ(b.name(), "cuda");
    EXPECT_TRUE(b.needs_bvh());
    b.release();  // releasing an unprepared backend makes no CUDA call and does not throw
}

/// The known scenes match the CPU, with the BVH and brute force, lit and normal.
TEST(CudaBackend, KnownScenesMatchCpu) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    for (const char* name :
         {"known_two_spheres", "known_single_sphere", "known_degenerate_triangle"}) {
        const rt::SceneHost s = known_scene(name);
        const rt::Bvh bvh = build(s);
        for (rt::ShadeMode shade : {rt::ShadeMode::Lit, rt::ShadeMode::Normal}) {
            SCOPED_TRACE(std::string(name) + (shade == rt::ShadeMode::Lit ? " lit" : " normal"));
            const rt::Image cpu = render<rt::CpuBackend>(s, &bvh, kAllAovs, shade);
            const rt::Image gpu = render<rt::CudaBackend>(s, &bvh, kAllAovs, shade);
            expect_matches_cpu(cpu, gpu);
            // On the device, BVH traversal and brute force give identical hits too.
            expect_identical(gpu, render<rt::CudaBackend>(s, nullptr, kAllAovs, shade));
        }
    }
}

/// A generated scene (spheres and a terrain mesh with shared edges) matches the CPU through
/// the median BVH and the LBVH.
TEST(CudaBackend, GeneratedSceneMatchesCpu) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const rt::SceneHost s = generated_scene(20000, 320, 180);
    for (rt::BvhMethod method : {rt::BvhMethod::Median, rt::BvhMethod::LBVH}) {
        SCOPED_TRACE(method == rt::BvhMethod::LBVH ? "lbvh" : "median");
        const rt::Bvh bvh = build(s, method);
        expect_matches_cpu(render<rt::CpuBackend>(s, &bvh), render<rt::CudaBackend>(s, &bvh));
    }
}

/// render() is repeatable and bit-identical, the camera may move between frames, prepare() may
/// be called again, and release() is idempotent. Timings land in the CUDA slots.
TEST(CudaBackend, Lifecycle) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const rt::SceneHost s = generated_scene(5000, 160, 90);
    const rt::Bvh bvh = build(s);
    rt::CudaBackend b;
    rt::Timings t;
    b.prepare(input(s, &bvh), t);
    EXPECT_GT(t.alloc, 0.0);
    EXPECT_GT(t.upload, 0.0);
    rt::Image first, second, moved;
    rt::Timings frame;
    b.render(s.camera, first, frame);
    EXPECT_GT(frame.render, 0.0);
    EXPECT_GT(frame.render_kernel, 0.0);
    EXPECT_LE(frame.render_kernel, frame.render);
    EXPECT_GT(frame.download, 0.0);
    for (int i = 0; i < 3; ++i) {
        b.render(s.camera, second, frame);
        expect_identical(first, second);
    }
    rt::Camera cam = s.camera;
    cam.position = cam.position + rt::vec3{0.5f, 0.0f, 0.0f};
    b.render(cam, moved, frame);
    EXPECT_NE(std::memcmp(first.rgb.data(), moved.rgb.data(), first.rgb.size() * sizeof(float)), 0);
    b.prepare(input(s, nullptr), t);  // re-prepare with brute force
    b.render(s.camera, second, frame);
    expect_identical(first, second);
    b.release();
    b.release();
    expect_error([&] { b.render(s.camera, second, frame); }, "before prepare");
}

/// Only the requested AOVs are produced; the RGB is the same either way.
TEST(CudaBackend, AovsFollowSettings) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const rt::SceneHost s = known_scene();
    const rt::Bvh bvh = build(s);
    const rt::Image all = render<rt::CudaBackend>(s, &bvh, kAllAovs);
    const size_t pixels = size_t{s.camera.width} * s.camera.height;
    EXPECT_EQ(all.prim_id.size(), pixels);
    EXPECT_EQ(all.light_mask.size(), pixels);
    const rt::Image none = render<rt::CudaBackend>(s, &bvh, 0);
    EXPECT_TRUE(none.prim_id.empty());
    EXPECT_TRUE(none.light_mask.empty());
    EXPECT_EQ(std::memcmp(all.rgb.data(), none.rgb.data(), all.rgb.size() * sizeof(float)), 0);
    const rt::Image prim = render<rt::CudaBackend>(s, &bvh, rt::AOV_PRIM_ID);
    EXPECT_EQ(prim.prim_id, all.prim_id);
    EXPECT_TRUE(prim.light_mask.empty());
}

/// An empty scene uploads no arrays and renders the background.
TEST(CudaBackend, EmptyScene) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::SceneHost s = known_scene();
    s.spheres.clear();
    s.triangles.clear();
    const rt::Bvh bvh = build(s);
    ASSERT_TRUE(bvh.nodes.empty());
    const rt::Image a = render<rt::CudaBackend>(s, &bvh);
    expect_identical(a, render<rt::CudaBackend>(s, nullptr));
    expect_identical(a, render<rt::CpuBackend>(s, &bvh));
    for (uint32_t p : a.prim_id) ASSERT_EQ(p, rt::RT_MISS_PRIM);
}

/// DeviceScene views carry the counts, null pointers for empty arrays, and num_nodes = 0
/// without a BVH; release() clears them.
TEST(DeviceScene, ViewsAndRelease) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    rt::SceneHost s = known_scene();
    s.triangles.clear();
    const rt::Bvh bvh = build(s);
    rt::DeviceScene d;
    rt::Timings t;
    d.upload(s, &bvh, {}, t);
    rt::Scene v = d.scene_view();
    EXPECT_EQ(v.num_spheres, s.spheres.size());
    EXPECT_NE(v.spheres, nullptr);
    EXPECT_NE(v.spheres, s.spheres.data());
    EXPECT_EQ(v.num_triangles, 0u);
    EXPECT_EQ(v.triangles, nullptr);
    EXPECT_EQ(v.num_lights, s.lights.size());
    EXPECT_EQ(v.ambient, s.ambient);
    EXPECT_EQ(d.bvh_view().num_nodes, bvh.nodes.size());
    d.upload(s, nullptr, {}, t);  // re-upload without a BVH releases the previous one
    EXPECT_EQ(d.bvh_view().num_nodes, 0u);
    EXPECT_EQ(d.bvh_view().nodes, nullptr);
    d.release();
    d.release();
    EXPECT_EQ(d.scene_view().spheres, nullptr);
    EXPECT_EQ(d.scene_view().num_spheres, 0u);

    rt::MemConfig managed;
    managed.mode = rt::MemMode::Managed;
    expect_error([&] { d.upload(s, nullptr, managed, t); }, "only --mem explicit");
}

TEST(CudaBackend, RejectsInvalidInput) {
    const rt::SceneHost s = known_scene();
    rt::CudaBackend b;
    rt::Timings t;
    rt::Image img;
    expect_error([&] { b.render(s.camera, img, t); }, "before prepare");

    rt::PrepareInput in = input(s, nullptr);
    in.scene = nullptr;
    expect_error([&] { b.prepare(in, t); }, "scene is null");
    in = input(s, nullptr);
    in.render.width = 0;
    expect_error([&] { b.prepare(in, t); }, "width and height");
    in = input(s, nullptr, 1u << 5);
    expect_error([&] { b.prepare(in, t); }, "unknown AOV");
    in = input(s, nullptr, rt::AOV_PRIM_ID, static_cast<rt::ShadeMode>(7));
    expect_error([&] { b.prepare(in, t); }, "unknown shade mode");

    rt::SceneHost many_lights = s;
    many_lights.lights.assign(33, s.lights.empty() ? rt::Light{} : s.lights[0]);
    expect_error([&] { b.prepare(input(many_lights, nullptr), t); }, "at most 32 lights");

    rt::SceneHost other = s;
    other.spheres.pop_back();
    const rt::Bvh wrong = build(other);
    expect_error([&] { b.prepare(input(s, &wrong), t); }, "primitive references");

    // A failed prepare leaves the backend unprepared.
    expect_error([&] { b.render(s.camera, img, t); }, "before prepare");
}

/// The camera must have the prepared resolution; a change needs a new prepare().
TEST(CudaBackend, RejectsResolutionChange) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const rt::SceneHost s = known_scene();
    rt::CudaBackend b;
    rt::Timings t;
    b.prepare(input(s, nullptr), t);
    rt::Camera cam = s.camera;
    cam.width += 1;
    rt::Image img;
    expect_error([&] { b.render(cam, img, t); }, "differs from the prepared");
}

/// A BVH deeper than 64 levels makes render() fail through the device overflow flag instead
/// of returning a silently wrong image. The flag is cleared per frame, so a valid BVH prepared
/// afterwards renders normally.
TEST(CudaBackend, StackOverflowFailsRender) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
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
    s.camera.look_at = sp.center;  // aim every pixel at the sphere so each ray enters the chain
    s.camera.vfov_deg = 1.0f;
    rt::CudaBackend b;
    rt::Timings t;
    b.prepare(input(s, &bvh), t);
    rt::Image img;
    expect_error([&] { b.render(s.camera, img, t); }, "stack overflow");
    expect_error([&] { b.render(s.camera, img, t); }, "stack overflow");
    const rt::Bvh good = build(s);
    b.prepare(input(s, &good), t);
    EXPECT_NO_THROW(b.render(s.camera, img, t));
}
}  // namespace

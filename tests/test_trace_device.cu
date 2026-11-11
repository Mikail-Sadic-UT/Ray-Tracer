/// @file test_trace_device.cu
/// @brief Runs the trace functions in CUDA kernels and compares the results with the host.
///
/// Built only when RT_ENABLE_CUDA is on. Cases that need a GPU skip when no CUDA device is
/// visible (RT_SKIP_WITHOUT_CUDA_DEVICE); host-only fixture checks always run.
// brute.hpp comes first, so this file also checks that it compiles as CUDA on its own.
#include "trace/brute.hpp"
// Other headers follow.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "camera/camera.hpp"
#include "cuda_test_util.hpp"
#include "gpu/cuda_check.hpp"

namespace {

constexpr float kInf = INFINITY;

// Host and device evaluate the same expressions, but nvcc contracts a*b + c into FMA by default
// (--fmad=true) and GCC may contract differently. The triangle test uses explicit fmaf, so its
// results must match bit for bit; these tolerances cover only the sphere math. Scene coordinates
// lie within +-10, so every t here is below 40.
constexpr float kTolT = 1.0e-4f;   ///< Absolute tolerance on t from the same ray.
constexpr float kTolUv = 1.0e-5f;  ///< Absolute tolerance on barycentrics.

/// Copies @p v to a new device array. The caller frees it with cudaFree.
template <class T>
T* to_device(const std::vector<T>& v) {
    T* d = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d, v.size() * sizeof(T)));
    RT_CUDA_CHECK(cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice));
    return d;
}

/// Copies @p n elements of device array @p d back to the host.
template <class T>
std::vector<T> to_host(const T* d, size_t n) {
    std::vector<T> v(n);
    RT_CUDA_CHECK(cudaMemcpy(v.data(), d, n * sizeof(T), cudaMemcpyDeviceToHost));
    return v;
}

/// Waits for the last kernel and turns a launch or execution error into rt::Error.
void sync_kernel() {
    RT_CUDA_CHECK(cudaGetLastError());
    RT_CUDA_CHECK(cudaDeviceSynchronize());
}

/// Expects equal values, or values within @p tol when both are finite.
void expect_close(float device, float host, float tol) {
    if (std::isfinite(host)) {
        EXPECT_NEAR(device, host, tol);
    } else {
        EXPECT_EQ(device, host);
    }
}

// ---------------------------------------------------------------------------------------------
// Trace functions on fixed rays
// ---------------------------------------------------------------------------------------------

constexpr int kNumSpheres = 2;
constexpr int kNumTriangles = 3;

/// Fixed spheres: one tied with kT0 for the ray through (0.25, 0.25), one off to the side.
const std::vector<rt::Sphere> kSpheres{{{0.25f, 0.25f, 1.0f}, 1.0f, 0},
                                       {{5.0f, 5.0f, 5.0f}, 1.5f, 0}};

/// Fixed triangles: the shared-edge pair from the CPU tests and one tilted triangle.
const std::vector<rt::Triangle> kTriangles{
    {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0},
    {{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0},
    {{-3.0f, -2.0f, 6.0f}, {3.0f, -1.0f, 7.0f}, {0.0f, 3.0f, 8.0f}, 0}};

/// Fixed rays. The two oblique rays have non-dyadic directions and pass at least 1e-2 from
/// any edge, tangent, or interval boundary, so contraction differences cannot flip a hit.
const std::vector<rt::Ray> kRays{
    {{5.0f, 5.0f, -5.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kInf},     // sphere 1 from outside
    {{20.0f, 20.0f, -5.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kInf},   // misses everything
    {{5.0f, 5.0f, 5.0f}, 0.0f, {0.0f, 1.0f, 0.0f}, kInf},      // inside sphere 1
    {{0.1f, 0.6f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kInf},     // triangle 0 interior
    {{0.5f, 0.5f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kInf},     // exact shared edge
    {{0.0f, 0.0f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kInf},     // exact vertex of triangle 0
    {{0.25f, 0.25f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, kInf},   // sphere 0 / triangle 0 tie
    {{0.1f, 0.6f, -1.0f}, 0.0f, {0.0f, 0.0f, 1.0f}, 0.5f},     // occluder beyond tmax
    {{-2.0f, 1.0f, 12.0f}, 0.0f, {2.0f, -1.0f, -5.0f}, kInf},  // oblique, triangle 2 centroid
    {{4.0f, 6.0f, -2.0f}, 0.0f, {1.0f, -1.0f, 7.0f}, kInf},    // oblique, sphere 1 center
};

/// One primitive test's output.
struct PrimResult {
    int hit;
    float t, u, v;
};

/// Everything the trace module reports for one ray.
struct CaseResult {
    PrimResult sphere[kNumSpheres];
    PrimResult triangle[kNumTriangles];
    rt::Hit closest;        ///< brute_closest
    int any;                ///< brute_any
    rt::Hit query_closest;  ///< BruteQuery::closest
    int query_any;          ///< BruteQuery::any
};

/// Runs every trace function on one ray. The same code runs on the host and in the kernel.
RT_INLINE CaseResult trace_case(const rt::Ray& r, const rt::Scene& s) {
    CaseResult c{};
    for (int i = 0; i < kNumSpheres; ++i) {
        PrimResult& p = c.sphere[i];
        p.hit = rt::intersect_sphere(r, s.spheres[i], r.tmax, p.t);
    }
    for (int i = 0; i < kNumTriangles; ++i) {
        PrimResult& p = c.triangle[i];
        p.hit = rt::intersect_triangle(r, s.triangles[i], r.tmax, p.t, p.u, p.v);
    }
    c.closest = rt::brute_closest(r, s);
    c.any = rt::brute_any(r, s);
    const rt::BruteQuery q{s};
    c.query_closest = q.closest(r);
    c.query_any = q.any(r);
    return c;
}

/// One thread per ray.
__global__ void trace_kernel(const rt::Ray* rays, int n, rt::Scene s, CaseResult* out) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[i] = trace_case(rays[i], s);
}

/// Scene view over arrays in host or device memory; materials and lights are unused.
rt::Scene trace_scene(const rt::Sphere* spheres, const rt::Triangle* triangles) {
    rt::Scene s{};
    s.spheres = spheres;
    s.num_spheres = kNumSpheres;
    s.triangles = triangles;
    s.num_triangles = kNumTriangles;
    return s;
}

/// Compares one primitive test's device and host output: triangles exactly, spheres' t within
/// tolerance.
void expect_prim(const PrimResult& d, const PrimResult& h, bool triangle) {
    ASSERT_EQ(d.hit, h.hit);
    if (!h.hit) return;
    if (triangle) {
        EXPECT_EQ(d.t, h.t);
        EXPECT_EQ(d.u, h.u);
        EXPECT_EQ(d.v, h.v);
    } else {
        EXPECT_NEAR(d.t, h.t, kTolT);
    }
}

/// Compares one Hit: prim exactly, then t / u / v exactly for a triangle hit and within
/// tolerance otherwise.
void expect_hit(const rt::Hit& d, const rt::Hit& h) {
    ASSERT_EQ(d.prim, h.prim);
    if (h.prim != rt::RT_MISS_PRIM && (h.prim & rt::PRIM_TRIANGLE_BIT) != 0) {
        EXPECT_EQ(d.t, h.t);
        EXPECT_EQ(d.u, h.u);
        EXPECT_EQ(d.v, h.v);
        return;
    }
    expect_close(d.t, h.t, kTolT);
    EXPECT_NEAR(d.u, h.u, kTolUv);
    EXPECT_NEAR(d.v, h.v, kTolUv);
}

/// intersect_*, brute_*, and BruteQuery give the same answers in a kernel as on the host.
TEST(TraceDevice, MatchesHost) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const int n = static_cast<int>(kRays.size());
    rt::Sphere* d_spheres = to_device(kSpheres);
    rt::Triangle* d_triangles = to_device(kTriangles);
    rt::Ray* d_rays = to_device(kRays);
    CaseResult* d_out = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_out, n * sizeof(CaseResult)));

    trace_kernel<<<1, 32>>>(d_rays, n, trace_scene(d_spheres, d_triangles), d_out);
    sync_kernel();
    const std::vector<CaseResult> device = to_host(d_out, n);
    RT_CUDA_CHECK(cudaFree(d_out));
    RT_CUDA_CHECK(cudaFree(d_rays));
    RT_CUDA_CHECK(cudaFree(d_triangles));
    RT_CUDA_CHECK(cudaFree(d_spheres));

    const rt::Scene host_scene = trace_scene(kSpheres.data(), kTriangles.data());
    for (int i = 0; i < n; ++i) {
        SCOPED_TRACE("ray " + std::to_string(i));
        const CaseResult h = trace_case(kRays[i], host_scene);
        const CaseResult& d = device[i];
        for (int k = 0; k < kNumSpheres; ++k) expect_prim(d.sphere[k], h.sphere[k], false);
        for (int k = 0; k < kNumTriangles; ++k) expect_prim(d.triangle[k], h.triangle[k], true);
        expect_hit(d.closest, h.closest);
        EXPECT_EQ(d.any, h.any);
        expect_hit(d.query_closest, h.query_closest);
        EXPECT_EQ(d.query_any, h.query_any);
    }
}

/// The fixed rays exercise the cases they are meant to: hits, a miss, the tie, and tmax.
TEST(TraceDevice, FixturesCoverIntendedCases) {
    const rt::Scene s = trace_scene(kSpheres.data(), kTriangles.data());
    const rt::PrimRef tri = rt::PRIM_TRIANGLE_BIT;
    const rt::PrimRef expect[] = {1u, rt::RT_MISS_PRIM, 1u,       tri, tri, tri,
                                  0u, rt::RT_MISS_PRIM, tri | 2u, 1u};
    ASSERT_EQ(kRays.size(), sizeof(expect) / sizeof(expect[0]));
    for (size_t i = 0; i < kRays.size(); ++i) {
        EXPECT_EQ(rt::brute_closest(kRays[i], s).prim, expect[i]) << "ray " << i;
    }
}

// ---------------------------------------------------------------------------------------------
// Camera rays generated on the device and traced in the same kernel
// ---------------------------------------------------------------------------------------------

// Device and host tanf differ by a few ulp, and contraction differs, so the ray components
// can differ by about 2e-6; 1e-5 leaves a factor of 5. A direction error d changes the wall
// distance t = 10 / |dir.z| by about 10 d / dir.z^2, at most about 3e-4 at the corners
// (|dir.z| ~ 0.58), so t gets 1e-3.
constexpr float kTolRay = 1.0e-5f;   ///< Absolute tolerance on ray origin and direction.
constexpr float kTolCamT = 1.0e-3f;  ///< Absolute tolerance on t for device-generated rays.

/// 64x64 camera on the +z axis looking at the origin with a 90 degree field of view, so a
/// pixel's image-plane point is `(sx, sy) = ((2 px + 1) / 64 - 1, 1 - (2 py + 1) / 64)`.
const rt::Camera kCam{{0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 90.0f, 64, 64};

/// Sphere 0 on the view axis; its silhouette is the image-plane circle of radius 2/sqrt(21).
const std::vector<rt::Sphere> kCamSpheres{{{0.0f, 0.0f, 0.0f}, 2.0f, 0}};

/// Diamond-shaped back wall at z = -5, split along x = 5 (image column sx = 0.5).
const std::vector<rt::Triangle> kCamTriangles{
    {{5.0f, -40.0f, -5.0f}, {5.0f, 40.0f, -5.0f}, {-35.0f, 0.0f, -5.0f}, 0},  // left, 0x80000000
    {{5.0f, -40.0f, -5.0f}, {45.0f, 0.0f, -5.0f}, {5.0f, 40.0f, -5.0f}, 0}};  // right, 0x80000001

/// A sampled pixel and the primitive its ray must hit.
struct CamPixel {
    uint32_t px, py;
    rt::PrimRef prim;
};

/// The image center, the four corners, and four interior pixels. The smallest clearance from a
/// sphere silhouette or wall edge is 0.082 in image-plane units, far above kTolRay.
const std::vector<CamPixel> kCamPixels{
    {32, 32, 0u},         {0, 0, 0x80000000u},   {63, 0, 0x80000001u},
    {0, 63, 0x80000000u}, {63, 63, 0x80000001u}, {40, 24, 0u},
    {24, 40, 0u},         {8, 32, 0x80000000u},  {52, 12, 0x80000001u}};

/// Scene view of the camera check's sphere and wall.
rt::Scene cam_scene(const rt::Sphere* spheres, const rt::Triangle* triangles) {
    rt::Scene s{};
    s.spheres = spheres;
    s.num_spheres = static_cast<uint32_t>(kCamSpheres.size());
    s.triangles = triangles;
    s.num_triangles = static_cast<uint32_t>(kCamTriangles.size());
    return s;
}

/// One thread per pixel: generate the camera ray and trace it in the same kernel.
__global__ void camera_check_kernel(rt::Camera cam, const CamPixel* pixels, int n, rt::Scene s,
                                    rt::Ray* rays, rt::Hit* hits) {
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    const rt::Ray r = rt::camera_ray(cam, pixels[i].px, pixels[i].py);
    rays[i] = r;
    hits[i] = rt::brute_closest(r, s);
}

/// Device rays and hits for kCamPixels.
struct CamDeviceResult {
    std::vector<rt::Ray> rays;
    std::vector<rt::Hit> hits;
};

/// Runs camera_check_kernel on kCamPixels.
CamDeviceResult run_camera_kernel() {
    const int n = static_cast<int>(kCamPixels.size());
    rt::Sphere* d_spheres = to_device(kCamSpheres);
    rt::Triangle* d_triangles = to_device(kCamTriangles);
    CamPixel* d_pixels = to_device(kCamPixels);
    rt::Ray* d_rays = nullptr;
    rt::Hit* d_hits = nullptr;
    RT_CUDA_CHECK(cudaMalloc(&d_rays, n * sizeof(rt::Ray)));
    RT_CUDA_CHECK(cudaMalloc(&d_hits, n * sizeof(rt::Hit)));

    camera_check_kernel<<<1, 32>>>(kCam, d_pixels, n, cam_scene(d_spheres, d_triangles), d_rays,
                                   d_hits);
    sync_kernel();
    CamDeviceResult out{to_host(d_rays, n), to_host(d_hits, n)};
    RT_CUDA_CHECK(cudaFree(d_hits));
    RT_CUDA_CHECK(cudaFree(d_rays));
    RT_CUDA_CHECK(cudaFree(d_pixels));
    RT_CUDA_CHECK(cudaFree(d_triangles));
    RT_CUDA_CHECK(cudaFree(d_spheres));
    return out;
}

/// Host-only guard on the fixtures: each pixel hits its expected primitive and stays clear of
/// the sphere silhouette and the wall edges, so the device tolerances cannot change `prim`.
TEST(TraceDevice, CameraPixelsClearOfEdges) {
    const rt::Scene s = cam_scene(kCamSpheres.data(), kCamTriangles.data());
    const float silhouette = 2.0f / std::sqrt(21.0f);
    for (const CamPixel& p : kCamPixels) {
        SCOPED_TRACE("pixel " + std::to_string(p.px) + "," + std::to_string(p.py));
        const rt::Ray r = rt::camera_ray(kCam, p.px, p.py);
        EXPECT_EQ(rt::brute_closest(r, s).prim, p.prim);
        const float sx = r.dir.x / -r.dir.z;
        const float sy = r.dir.y / -r.dir.z;
        EXPECT_GE(std::fabs(std::sqrt(sx * sx + sy * sy) - silhouette), 0.05f);
        if (p.prim != 0u) {
            const float x = 10.0f * sx, y = 10.0f * sy;  // wall point
            EXPECT_GE(std::fabs(x - 5.0f), 1.0f);        // shared edge x = 5
            EXPECT_GE((40.0f - std::fabs(x - 5.0f) - std::fabs(y)) / std::sqrt(2.0f), 1.0f);
        }
    }
}

/// camera_ray gives the same ray in a kernel as on the host.
TEST(TraceDevice, CameraRayMatchesHost) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const CamDeviceResult d = run_camera_kernel();
    for (size_t i = 0; i < kCamPixels.size(); ++i) {
        SCOPED_TRACE("pixel " + std::to_string(i));
        const rt::Ray h = rt::camera_ray(kCam, kCamPixels[i].px, kCamPixels[i].py);
        const rt::Ray& r = d.rays[i];
        for (int a = 0; a < 3; ++a) {
            EXPECT_NEAR(r.origin[a], h.origin[a], kTolRay);
            EXPECT_NEAR(r.dir[a], h.dir[a], kTolRay);
        }
        EXPECT_EQ(r.tmin, h.tmin);
        EXPECT_EQ(r.tmax, h.tmax);
    }
}

/// brute_closest on device-generated camera rays hits the same primitive as on the host.
TEST(TraceDevice, CameraRayClosestMatchesHost) {
    RT_SKIP_WITHOUT_CUDA_DEVICE();
    const CamDeviceResult d = run_camera_kernel();
    const rt::Scene s = cam_scene(kCamSpheres.data(), kCamTriangles.data());
    for (size_t i = 0; i < kCamPixels.size(); ++i) {
        SCOPED_TRACE("pixel " + std::to_string(i));
        const rt::Hit h =
            rt::brute_closest(rt::camera_ray(kCam, kCamPixels[i].px, kCamPixels[i].py), s);
        EXPECT_EQ(d.hits[i].prim, h.prim);
        EXPECT_NEAR(d.hits[i].t, h.t, kTolCamT);
    }
}

}  // namespace

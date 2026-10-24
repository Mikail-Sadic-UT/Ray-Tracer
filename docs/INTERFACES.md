# Interfaces (shared contracts)

**Status: PROPOSED.** Finalize at the team contract meeting, Sept 24–25. After sign-off, changes follow the [contract-change process](#contract-change-process).

Everything here is shared by at least two members. Signatures are sketches. Field order and sizes are what matter.

| Contract | Owner | Consumers |
|---|---|---|
| [Math library](#1-math-library) | Arek | All |
| [Scene struct + file format](#2-scene-struct-and-file-format) | Po | All |
| [BVH layout](#3-bvh-layout) | Jett | Mikail, Po |
| [Traversal + intersection](#4-traversal-and-intersection) | Mikail | Arek, Po |
| [Shading](#5-shading) | Arek | Mikail, Po |
| [Timing/results schema](#6-timing-and-results-schema) | Arek | All |

General rules:

- C++17. All shared types are POD, header-only, and usable in device code.
- No `std::` containers, exceptions, or virtual calls in `RT_HOSTDEV` code.
- Indices are `uint32_t`. Sizes on disk are little-endian (x86 and ARM both are).
- Use `static_assert(sizeof(T) == N)` for every on-disk or on-device struct.

## 1. Math library

Owner: Arek.

```cpp
// hostdev.hpp
#ifdef __CUDACC__
  #define RT_HOSTDEV __host__ __device__
#else
  #define RT_HOSTDEV
#endif
#define RT_INLINE RT_HOSTDEV inline

struct vec3 { float x, y, z; };          // 12 B; ops: + - * / dot cross length normalize min max
using color = vec3;                       // linear RGB, [0, inf)

struct Ray {                              // 32 B
  vec3  origin;  float tmin;
  vec3  dir;     float tmax;              // dir need not be normalized; document per use
};

struct AABB {                             // 24 B
  vec3 lo, hi;                            // empty box: lo = +inf, hi = -inf
  // ops: grow(vec3), grow(AABB), centroid(), surface_area(), intersect(ray, inv_dir, tmin, tmax)
};
```

Float conventions:

- `float` only, including the CPU reference. No `double` in shared code.
- No `-ffast-math` / `--use_fast_math`.
- Handle `inv_dir` with IEEE infinities (do not special-case zero components).

## 2. Scene struct and file format

Owner: Po.

### In-memory layout

```cpp
struct Sphere   { vec3 center; float radius; uint32_t material_id; };      // 20 B
struct Triangle { vec3 v0, v1, v2; uint32_t material_id; };                // 40 B

enum MaterialFlags : uint32_t { MAT_SPECULAR = 1u << 0 /* reserved: MAT_MIRROR = 1u << 1 */ };
struct Material { color albedo; color ks; float shininess; uint32_t flags; };  // 32 B

enum LightType : uint32_t { LIGHT_POINT = 0, LIGHT_DIRECTIONAL = 1 };
struct Light {                                                             // 32 B
  uint32_t type;
  vec3 pos_or_dir;     // point: position; directional: direction light travels
  color intensity;     // point: radiant intensity (falls off 1/d^2); directional: irradiance
  float pad;
};

struct Camera {
  vec3 position, look_at, up;
  float vfov_deg;      // vertical field of view
  uint32_t width, height;
};

struct Scene {         // non-owning views; storage owned by scene_io / DeviceMemory
  const Sphere*   spheres;   uint32_t num_spheres;
  const Triangle* triangles; uint32_t num_triangles;
  const Material* materials; uint32_t num_materials;
  const Light*    lights;    uint32_t num_lights;
  color background;
  float ambient;       // constant ambient term
};
```

Coordinate convention: right-handed, **Y-up** (`obj2bin` converts Blender's Z-up).

### JSON: inline geometry (small test scenes)

```json
{
  "version": 1,
  "camera": { "position": [0, 1, 5], "look_at": [0, 0.5, 0], "up": [0, 1, 0],
              "vfov_deg": 45, "width": 640, "height": 480 },
  "background": [0.1, 0.1, 0.15],
  "ambient": 0.05,
  "materials": [
    { "name": "red",   "albedo": [0.8, 0.1, 0.1], "ks": [0.5, 0.5, 0.5], "shininess": 64, "specular": true },
    { "name": "floor", "albedo": [0.7, 0.7, 0.7], "ks": [0, 0, 0],       "shininess": 1,  "specular": false }
  ],
  "lights": [
    { "type": "point",       "position":  [2, 4, 2],   "intensity": [20, 20, 20] },
    { "type": "directional", "direction": [-1, -2, -1], "intensity": [0.5, 0.5, 0.5] }
  ],
  "geometry": {
    "spheres":   [ { "center": [0, 0.5, 0], "radius": 0.5, "material": 0 } ],
    "triangles": [ { "v": [[-5,0,-5],[5,0,-5],[5,0,5]],  "material": 1 },
                   { "v": [[-5,0,-5],[5,0,5],[-5,0,5]],  "material": 1 } ]
  }
}
```

### JSON: binary geometry (generated / large scenes)

```json
{
  "version": 1,
  "camera": { "...": "as above" },
  "materials": [ "..." ],
  "lights": [ "..." ],
  "geometry": { "binary": "bench_1m_seed1.bin" },
  "generator": { "tool": "rt_gen", "objects": 1000000, "seed": 1, "mix": "spheres+terrain" }
}
```

`geometry.binary` is a path relative to the JSON file. `generator` is informational and lets anyone regenerate the `.bin`. A file must use either inline geometry or `binary`, not both.

### `.bin` layout

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | Magic `"RTSCENE\0"` |
| 8 | 4 | `version` (u32, = 1) |
| 12 | 4 | `header_size` (u32, bytes) |
| 16 | 4 | `num_spheres` (u32) |
| 20 | 4 | `num_triangles` (u32) |
| 24 | 8 | `spheres_offset` (u64, from file start) |
| 32 | 8 | `triangles_offset` (u64) |
| 40 | 24 | Reserved, zero |
| 64 | … | `Sphere[num_spheres]`, then `Triangle[num_triangles]` (raw structs, 16-byte aligned starts) |

Readers reject a wrong magic or unknown version. Materials and lights stay in JSON.

## 3. BVH layout

Owner: Jett.

```cpp
struct BvhNode {               // 32 B
  AABB bounds;                 // 24 B
  uint32_t left_or_first;      // interior: index of left child (right = left + 1); leaf: first prim_ref
  uint32_t count;              // 0 = interior; >0 = leaf with `count` prim_refs
};
static_assert(sizeof(BvhNode) == 32);

// High bit = type, low 31 bits = index into spheres[] or triangles[]
using PrimRef = uint32_t;
constexpr uint32_t PRIM_TRIANGLE_BIT = 0x80000000u;

enum class BvhMethod { Median, LBVH, SAH };

struct BvhBuildInput {         // built by caller from the Scene
  const AABB*    bounds;       // per primitive
  const vec3*    centroids;    // per primitive
  const PrimRef* refs;         // per primitive
  uint32_t       count;
};

struct BvhParams { BvhMethod method; uint32_t max_leaf_size = 4; uint32_t sah_bins = 16; };

struct Bvh {                   // owning on host
  std::vector<BvhNode> nodes;  // nodes[0] = root
  std::vector<PrimRef> prim_refs;
  uint32_t depth;              // must be <= RT_MAX_BVH_DEPTH
};

Bvh  build_bvh(const BvhBuildInput& in, const BvhParams& p);   // uses OpenMP internally
bool validate_bvh(const Bvh&, const BvhBuildInput&, std::string* err);
float sah_cost(const Bvh&);

constexpr int RT_MAX_BVH_DEPTH = 64;
```

The device-side view is `{const BvhNode* nodes; const PrimRef* prim_refs;}`. Validator checks: every primitive appears exactly once, child bounds contained in parent, leaf bounds contain their primitives, depth ≤ 64.

## 4. Traversal and intersection

Owner: Mikail.

```cpp
struct Hit {                   // 16 B
  float t;                     // INF on miss
  PrimRef prim;                // valid iff t < INF
  float u, v;                  // triangle barycentrics; unused for spheres
};

struct BvhView { const BvhNode* nodes; const PrimRef* prim_refs; };

RT_HOSTDEV bool intersect_sphere  (const Ray&, const Sphere&,   float& t);
RT_HOSTDEV bool intersect_triangle(const Ray&, const Triangle&, float& t, float& u, float& v);

RT_HOSTDEV Hit  traverse_closest(const Ray&, const Scene&, const BvhView&);
RT_HOSTDEV bool traverse_any    (const Ray&, const Scene&, const BvhView&);   // uses ray.tmax

// Brute-force versions for testing and early stubs:
RT_HOSTDEV Hit  brute_closest(const Ray&, const Scene&);
RT_HOSTDEV bool brute_any    (const Ray&, const Scene&);
```

- Fixed stack of `RT_MAX_BVH_DEPTH` entries. No recursion.
- Triangle test: Möller–Trumbore first; watertight (Woop et al.) if edge cracks show up. Decide in Task 3.
- Spheres: hits from inside count (needed for robustness tests).
- Normals are computed in `shade`, not here, to keep `Hit` small.

## 5. Shading

Owner: Arek.

```cpp
struct SurfacePoint { vec3 p; vec3 n; uint32_t material_id; };    // n faces the incoming ray

RT_HOSTDEV SurfacePoint surface_at(const Ray&, const Hit&, const Scene&);

// Robust origin offset (Ray Tracing Gems I, Ch. 6)
RT_HOSTDEV vec3 offset_ray_origin(vec3 p, vec3 n);

// Contribution of light `i` assuming it's visible, plus the shadow ray to test.
// Returns false if the light faces away (no shadow ray needed).
RT_HOSTDEV bool light_sample(const SurfacePoint&, const vec3& view_dir, const Scene&,
                             uint32_t i, color& contrib, Ray& shadow_ray);

RT_HOSTDEV color ambient_term(const SurfacePoint&, const Scene&);
```

Pixel color, shared by every backend:

```
c = ambient_term(sp)
for each light i:
    if light_sample(sp, v, scene, i, contrib, sray) and !traverse_any(sray): c += contrib
miss: c = scene.background
```

Shading model: `contrib = (albedo * max(0, n·l) + [MAT_SPECULAR] ks * max(0, n·h)^shininess) * I_i * falloff`. Falloff is `1/d²` for point lights and `1` for directional lights. Directional shadow rays have `tmax = INF`. Point-light shadow rays stop just short of the light.

Output to image: clamp to [0,1], then sRGB-encode for PNG. PFM stores linear unclamped floats.

## 6. Timing and results schema

Owner: Arek.

```cpp
struct Timings {               // milliseconds
  double load, build, upload, render, download, write;
  std::vector<std::pair<std::string,double>> render_stages;   // wavefront only
};
class ScopedTimer;             // steady_clock (CPU)
class GpuTimer;                // cudaEvent pair (GPU)
```

One JSON object per invocation. CSV has the same fields flattened (one row per run).

```json
{
  "machine": { "host": "c608-052", "cpu": "Grace", "gpu": "H100", "os": "Linux", "compiler": "gcc 13.2.0", "cuda": "12.x" },
  "commit": "abc1234", "timestamp": "2026-11-10T14:02:11Z",
  "config": { "scene": "bench_1m_seed1.json", "objects": 1000000, "width": 1920, "height": 1080,
              "lights": 2, "backend": "cuda", "pipeline": "mega", "mem": "explicit",
              "bvh": "sah", "threads": 72 },
  "cold": { "total": 0.0, "cuda_init": 0.0, "build": 0.0, "upload": 0.0, "render": 0.0, "download": 0.0 },
  "warm": { "median_total": 0.0, "median": { "build": 0.0, "upload": 0.0, "render": 0.0, "download": 0.0 },
            "runs": [ { "total": 0.0, "build": 0.0, "upload": 0.0, "render": 0.0, "download": 0.0, "stages": {} } ] },
  "load": 0.0,
  "bvh_stats": { "nodes": 0, "depth": 0, "sah_cost": 0.0 },
  "image": "out.png"
}
```

`total = build + upload + render + download`. `load` and `write` are excluded. Methodology is in [BENCHMARKING.md](BENCHMARKING.md).

## Precision and tolerance rules

- CPU and GPU compilers contract FMAs differently, so outputs differ slightly. Never require bit-identical CPU vs GPU output.
- **Same-backend determinism is required.** CPU 1 thread vs N threads, repeated BVH builds, and repeated GPU runs must be bit-identical.
- Default comparison threshold (tune at check-in 2): **≥ 99.9% of pixels within 2/255 per channel**, and report max error.
- Specular highlights may need a looser threshold, or a diffuse-only comparison mode.
- Compare in 8-bit sRGB space for the pass/fail threshold. Use PFM for detailed error analysis.

## Contract-change process

1. Open an issue labeled `contract` describing the change and who it affects.
2. PR edits this file and the code together.
3. All four members review. Merge requires every member's approval.
4. Bump the `.bin` / JSON `version` field if the file format changes.

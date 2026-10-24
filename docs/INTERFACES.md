# Interfaces (shared contracts)

**Status: v0.3, PROPOSED (Oct 24).** Sign-off target: **Oct 27**. v0.2 froze numeric rules (intervals, tie-breaks, NaN handling, shadow rays), defined owning vs view types, and specified how every builder reaches the shared node layout. v0.3 changes:
- fixes the slab test's signed-zero case;
- adds `RenderSettings`, device BVH ownership, `PixelResult`, AOV semantics, and the `.u32img` format;
- switches the scene hash to a canonical encoding;
- adds a task-parallel median contingency;
- adds compile-time tracing queries (`BvhQuery`, `BruteQuery`, and later Embree/OptiX), so shading no longer hardwires BVH traversal;
- adds fresh-page `system` and a separate `system-reuse` memory mode. After sign-off, changes follow the [contract-change process](#contract-change-process).

Signatures are sketches. Field order, sizes, and the stated rules are what matter.

| Contract | Owner | Consumers |
|---|---|---|
| [Math library](#1-math-library) | Arek | All |
| [Scene, image, and file formats](#2-scene-image-and-file-formats) | Po | All |
| [BVH layout and build pipeline](#3-bvh-layout-and-build-pipeline) | Jett | Mikail, Po |
| [Camera](#4-camera) | Jett | All |
| [Intersection and traversal](#5-intersection-and-traversal) | Mikail | Arek, Po |
| [Shading](#6-shading) | Arek | Mikail, Po |
| [Backend interface](#7-backend-interface) | Mikail | All |
| [Timing and results schema](#8-timing-and-results-schema) | Arek | All |

General rules:

- C++17. Types used in device code are POD, header-only, and contain no `std::` containers, exceptions, or virtual calls.
- Indices are `uint32_t`. On-disk data is little-endian.
- `static_assert(sizeof(T) == N)` for every on-disk or on-device struct.
- `float` only in shared render code, including the CPU reference. No `-ffast-math` / `--use_fast_math` anywhere (NaN comparisons below depend on IEEE semantics).
- FMA contraction differs between compilers, so CPU and GPU outputs differ slightly. Comparison rules are in [BENCHMARKING.md](BENCHMARKING.md#quality-protocol).

## 1. Math library

Owner: Arek.

```cpp
#ifdef __CUDACC__
  #define RT_HOSTDEV __host__ __device__
#else
  #define RT_HOSTDEV
#endif
#define RT_INLINE RT_HOSTDEV inline

struct vec3 { float x, y, z; };          // 12 B; + - * / dot cross length normalize min max, operator[]
using color = vec3;                       // linear RGB

struct Ray {                              // 32 B
  vec3 origin; float tmin;
  vec3 dir;    float tmax;
};

struct AABB { vec3 lo, hi; };             // 24 B; empty = {+inf, -inf}; grow, centroid, surface_area

constexpr float RT_EPS    = 5.9604645e-8f;                      // 2^-24
constexpr float RT_GAMMA3 = (3 * RT_EPS) / (1 - 3 * RT_EPS);    // PBRT gamma(3)
```

### Ray-box slab test (frozen)

`inv_dir = 1.0f / dir` per component, computed once per ray. A zero component gives `+inf` for `+0` and `-inf` for `-0`. When the origin lies exactly on a slab plane, `0 * ±inf = NaN`.

Near and far planes are chosen **from the sign of `inv_dir` before multiplying**, never by comparing the products. Comparing products fails when one of them is NaN. For example, with box `[0,1]³`, origin `(0, 0.5, 0.5)`, and direction `(-0.0, 1, 0)`, comparing gives `tn = NaN`, `tf = -inf`, no swap, and a false miss. Selecting by sign gives `tn = -inf`, `tf = NaN`: the NaN comparisons leave the interval unchanged, and the ray correctly hits. Boxes are closed (boundary inclusive).

```cpp
struct RayPrecomp { vec3 inv_d; int neg[3]; };   // neg[a] = (inv_d[a] < 0), computed once per ray

RT_INLINE RayPrecomp precompute(const Ray& r) {
  RayPrecomp p;
  p.inv_d = vec3{1.0f / r.dir.x, 1.0f / r.dir.y, 1.0f / r.dir.z};
  for (int a = 0; a < 3; ++a) p.neg[a] = p.inv_d[a] < 0.0f;   // -0 dir -> -inf -> neg = 1
  return p;
}

RT_INLINE bool intersect_aabb(const AABB& b, const vec3& o, const RayPrecomp& p,
                              float t_lo, float t_hi, float& t_enter) {
  float t0 = t_lo, t1 = t_hi;
  for (int a = 0; a < 3; ++a) {
    const float near_plane = p.neg[a] ? b.hi[a] : b.lo[a];
    const float far_plane  = p.neg[a] ? b.lo[a] : b.hi[a];
    float tn = (near_plane - o[a]) * p.inv_d[a];
    float tf = (far_plane  - o[a]) * p.inv_d[a];
    tf *= 1.0f + 2.0f * RT_GAMMA3;        // conservative far bound (PBRT v4, Sec. 6.1)
    t0 = tn > t0 ? tn : t0;               // keep this form: NaN -> unchanged
    t1 = tf < t1 ? tf : t1;
    if (t0 > t1) return false;
  }
  t_enter = t0;
  return true;
}
```

Why it works for a zero component on axis `a` (finite origin, non-empty box):

| Origin on axis `a` | `tn` | `tf` | Effect |
|---|---|---|---|
| Strictly inside the slab | `-inf` | `+inf` | Unconstrained |
| On either face | one is NaN, the other is `∓inf` toward "inside" | | Unconstrained (hit) |
| Outside the slab | Both `+inf` or both `-inf` | | `t0 = +inf` or `t1 = -inf` → miss |

Required tests (CI):

- For each axis, both signs of zero, and both faces (12 cases): ray lying on the face → hit.
- For each axis and both signs of zero: origin just outside each face → miss (12 cases).
- The reviewer's case above, verbatim.
- Ray starting inside → hit with `t_enter = t_lo`.
- Box entirely behind the origin → miss.
- Non-axis-aligned rays agree with a double-precision reference on random boxes.

## 2. Scene, image, and file formats

Owner: Po.

### Primitive and material types

```cpp
struct Sphere   { vec3 center; float radius; uint32_t material_id; };      // 20 B
struct Triangle { vec3 v0, v1, v2; uint32_t material_id; };                // 40 B

enum MaterialFlags : uint32_t { MAT_SPECULAR = 1u << 0 /* Extension; reserved: MAT_MIRROR = 1u << 1 */ };
struct Material { color albedo; color ks; float shininess; uint32_t flags; };  // 32 B

enum LightType : uint32_t { LIGHT_POINT = 0 /* Core */, LIGHT_DIRECTIONAL = 1 /* Extension */ };
struct Light { uint32_t type; vec3 pos_or_dir; color intensity; float pad = 0.0f; };  // 32 B
```

Every padding or reserved field is default-initialized to zero and written as zero in files. Hashes never read padding (see [Scene hash](#scene-hash)).

Coordinates: right-handed, **Y-up**.

### Owning storage vs views

Three types, so nobody passes host pointers to a kernel by accident:

```cpp
// Host, owning. Produced by scene_io / rt_gen. Lives for the whole run.
struct SceneHost {
  std::vector<Sphere> spheres; std::vector<Triangle> triangles;
  std::vector<Material> materials; std::vector<Light> lights;
  Camera camera; color background; float ambient;
  uint64_t hash;                        // see "Scene hash"
  Scene view() const;                   // host-pointer view
};

// Non-owning POD view. Passed by value to RT_HOSTDEV code and kernels.
struct Scene {
  const Sphere* spheres;     uint32_t num_spheres;
  const Triangle* triangles; uint32_t num_triangles;
  const Material* materials; uint32_t num_materials;
  const Light* lights;       uint32_t num_lights;
  color background; float ambient;
};

// Device, owning (Mikail). Owns the device copies of the scene AND the BVH.
class DeviceScene {
 public:
  // One allocation + one copy per non-empty array: spheres, triangles, materials,
  // lights, bvh.nodes, bvh.prim_refs. bvh may be null when no BVH is used: backends with
  // needs_bvh() == false, or CPU/CUDA in brute-force mode (--bvh none, BruteQuery).
  void upload(const SceneHost&, const Bvh* bvh, const MemConfig&, Timings&);  // times alloc, upload
  Scene   scene_view() const;               // device-pointer views
  BvhView bvh_view() const;                 // num_nodes = 0 if no BVH was uploaded
  void release();                           // idempotent
};
```

- **Uploads are per array**: six copies, not one memcpy. A packed single buffer is a possible later optimization. If it's done, it rebuilds the views with offsets into the packed buffer.
- The CUDA backend owns one `DeviceScene`. The CPU backend uses `SceneHost::view()` and a host `BvhView` directly, so it has no device copy.
- Under `--mem=system` (Extension E1), `upload` copies every array into **fresh** host pages: `mmap(MAP_ANONYMOUS)`, written by the CPU, released with `munmap`. Each preparation therefore starts from newly CPU-touched memory, never pages a previous run may have migrated. Under `--mem=system-reuse`, the views point directly into the persistent `SceneHost` and `Bvh` vectors, with no copy. That's a separate experiment, recorded as `geometry_reused: true`. See the [memory-mode protocol](BENCHMARKING.md#memory-mode-protocol-extension-e1-gh-node-only).
- The caller keeps `SceneHost` and `Bvh` alive and unmodified for the backend's lifetime in every mode, for simplicity.
- Empty arrays get a null pointer and count 0.

### Image

```cpp
struct Image {                          // host, owned by the caller
  uint32_t width = 0, height = 0;
  std::vector<float> rgb;               // linear, row-major, row 0 = top, 3 floats/pixel
  std::vector<uint32_t> prim_id;        // AOV, empty unless requested
  std::vector<uint32_t> light_mask;     // AOV, empty unless requested
};
```

`render()` resizes the `Image` to the prepared resolution. PNG output clamps to [0,1] and sRGB-encodes. PFM output stores the linear unclamped values.

### AOV semantics

Both AOVs come from the same `PixelResult` that produced the pixel color (see [section 6](#pixel-result)). No backend retraces rays to fill them.

| AOV | Hit pixel | Miss pixel |
|---|---|---|
| `prim_id` | `PrimRef` of the accepted primary hit | `0xFFFFFFFF` |
| `light_mask` | Bit `i` set iff light `i` **contributed**: it faces the surface (`dot(ng, l) > 0`) **and** its shadow ray is unoccluded. Back-facing and occluded lights both leave the bit clear. Bits ≥ L are zero. | `0` |

- AOVs require `num_lights ≤ 32`. With more lights, requesting `lightmask` is a startup error.
- A pixel that flips between back-facing and lit (at the shading terminator) shows up as a mask edge. That's the intended behavior for the shadow-agreement check.

### AOV file format (`.u32img`)

Lossless, little-endian, one channel per file. Written and read by `src/image` (Po).

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | Magic `"RTU32IMG"` |
| 8 | 4 | `version` (u32, = 1) |
| 12 | 4 | `width` (u32) |
| 16 | 4 | `height` (u32) |
| 20 | 4 | `channels` (u32, = 1) |
| 24 | 8 | Reserved, zero |
| 32 | 4·W·H | `uint32` values, row-major, row 0 = top |

`rt_render --aov primid,lightmask --pfm out.pfm` writes `out.primid.u32img` and `out.lightmask.u32img` next to the PFM. `rt_compare` loads them by the same naming rule.

### JSON: inline geometry (small test scenes)

```json
{
  "version": 2,
  "camera": { "position": [0, 1, 5], "look_at": [0, 0.5, 0], "up": [0, 1, 0],
              "vfov_deg": 45, "width": 640, "height": 480 },
  "background": [0.1, 0.1, 0.15],
  "ambient": 0.05,
  "materials": [
    { "name": "red",   "albedo": [0.8, 0.1, 0.1], "ks": [0, 0, 0], "shininess": 1, "specular": false },
    { "name": "floor", "albedo": [0.7, 0.7, 0.7], "ks": [0, 0, 0], "shininess": 1, "specular": false }
  ],
  "lights": [ { "type": "point", "position": [2, 4, 2], "intensity": [20, 20, 20] } ],
  "geometry": {
    "spheres":   [ { "center": [0, 0.5, 0], "radius": 0.5, "material": 0 } ],
    "triangles": [ { "v": [[-5,0,-5],[-5,0,5],[5,0,5]], "material": 1 },
                   { "v": [[-5,0,-5],[5,0,5],[5,0,-5]], "material": 1 } ]
  }
}
```

### JSON: binary geometry (generated scenes)

```json
{
  "version": 2,
  "camera": { "...": "as above" },
  "materials": [ "..." ], "lights": [ "..." ],
  "geometry": { "binary": "bench_1m_s1.bin" },
  "generator": {
    "command": "rt_gen --spheres 500000 --terrain-tris 500000 --seed 1 --out bench_1m_s1",
    "seed": 1, "num_spheres": 500000, "num_triangles": 500000
  }
}
```

The `generator.command` field records the exact arguments. Scene size is always stated in **primitives** (spheres + triangles), never "objects". An icosphere of subdivision level k contributes 20·4^k triangles.

### `.bin` layout

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | Magic `"RTSCENE\0"` |
| 8 | 4 | `version` (u32, = 2) |
| 12 | 4 | `header_size` (u32) |
| 16 | 4 | `num_spheres` (u32) |
| 20 | 4 | `num_triangles` (u32) |
| 24 | 8 | `spheres_offset` (u64) |
| 32 | 8 | `triangles_offset` (u64) |
| 40 | 8 | `content_hash` (u64, FNV-1a of the two arrays) |
| 48 | 16 | Reserved, zero |
| 64 | … | `Sphere[]`, then `Triangle[]`, each starting 16-byte aligned |

Readers reject a wrong magic, an unknown version, truncated files, and a hash mismatch.

### Scene hash

64-bit FNV-1a over a **canonical field-by-field encoding**, never raw struct bytes:

- Each `uint32_t` is fed as 4 little-endian bytes. Each `float` is fed as the 4 little-endian bytes of its IEEE bit pattern. Padding and reserved fields are skipped.
- `content_hash` (in `.bin`): `num_spheres`, `num_triangles`, then every sphere `(center.xyz, radius, material_id)`, then every triangle `(v0.xyz, v1.xyz, v2.xyz, material_id)`.
- `SceneHost::hash` (scene hash): `content_hash`, then counts and fields of materials `(albedo.rgb, ks.rgb, shininess, flags)`, lights `(type, pos_or_dir.xyz, intensity.rgb)`, `background.rgb`, `ambient`.
- The **camera is excluded** from the scene hash. CLI overrides change only the camera, so the scene hash stays the same. Results record the **effective camera** (after overrides) in full under `config.camera`, along with the effective resolution.

Computed after load and recorded in every results file. Two runs measure the same scene only when their scene hashes match; they used the same view only when their effective cameras also match.

### Generator determinism

- Own PRNG (PCG32) and own `u32 → float` conversion. No `std::` distributions.
- Generator code is compiled with `-ffp-contract=off` so x86 and ARM produce identical bytes.
- Test: the same arguments produce the same `content_hash` on the laptop and on Vista.

## 3. BVH layout and build pipeline

Owner: Jett.

### Node layout (frozen)

```cpp
struct BvhNode {               // 32 B
  AABB bounds;                 // 24 B
  uint32_t left_or_first;      // interior: left child index, right = left + 1; leaf: first prim_ref
  uint32_t count;              // 0 = interior; > 0 = leaf with `count` prim_refs
};

using PrimRef = uint32_t;      // high bit: 1 = triangle; low 31 bits: index
constexpr uint32_t PRIM_TRIANGLE_BIT = 0x80000000u;
constexpr int RT_MAX_BVH_DEPTH = 64;   // root = depth 0; leaves at depth <= 64
constexpr int RT_FALLBACK_DEPTH = 32;  // see depth cap

struct Bvh {                   // host, owning
  std::vector<BvhNode> nodes;  // nodes[0] = root; empty if the scene is empty
  std::vector<PrimRef> prim_refs;
  BvhStats stats;              // nodes, leaves, depth, fallbacks, sah_cost
};
struct BvhView { const BvhNode* nodes; const PrimRef* prim_refs; uint32_t num_nodes; };
```

Siblings are adjacent. Median split and SAH produce this layout directly by allocating child pairs. **LBVH does not**, so it has an explicit layout conversion step (below).

### Canonical primitive order

Build input lists all spheres (indices 0..S-1), then all triangles (0..T-1). This equals ascending `PrimRef` order. Every tie-break below uses it.

### Build pipeline

Every method runs the same stages. All of them are timed as part of preparation (see [section 8](#8-timing-and-results-schema)).

| Stage | Timed as | Parallel | Work |
|---|---|---|---|
| Bounds | `bounds` | Yes | Per-primitive AABB, centroid, PrimRef |
| Method | `build` | Method-dependent | Median, LBVH, or SAH |
| Finalize | `layout` | Yes | Convert to `BvhNode` layout if needed, collapse leaves, enforce depth cap, compute stats |

```cpp
enum class BvhMethod { Median, LBVH, SAH };
struct BvhParams { BvhMethod method; uint32_t max_leaf_size = 4; uint32_t sah_bins = 16; int threads; };
Bvh  build_bvh(const Scene& scene, const BvhParams& p, Timings& t);
bool validate_bvh(const Bvh&, const Scene&, std::string* err);
```

### Methods

| Method | Tier | Rules |
|---|---|---|
| **Median** | Core, correctness baseline (serial). Task-parallel variant = contingency parallel builder | Split axis = largest centroid extent (ties x < y < z). Object median: `nth_element` at `count/2` with comparator `(centroid[axis], PrimRef)`. Halves the count at every level, so depth ≤ ceil(log2(N)). **Contingency:** with `build_threads > 1`, subtrees below the top levels are built as OpenMP tasks, with node slots assigned from subtree sizes. A median subtree's node count depends only on its primitive count, `f(n) = 1` if `n ≤ max_leaf_size`, else `1 + f(⌊n/2⌋) + f(n − ⌊n/2⌋)`, so every slot is known before recursion and doesn't depend on scheduling. The comparator is a strict total order, so the output is byte-identical to the serial build. Top-level partitions stay serial, which limits scaling (Amdahl). That's acceptable for a fallback, and reported as such. |
| **LBVH** (parallel) | **Core, required parallel builder** | 30-bit Morton codes (10 bits/axis). Quantize per axis: `q = min(floor((c - lo) / extent * 1024), 1023)`; `extent == 0` → `q = 0`. Stable LSD radix sort on `(morton)` over canonical order. Karras 2012 hierarchy with ties broken by sorted index. Bottom-up refit with atomic visit counters. |
| **Binned SAH** (parallel) | Extension | Bins 16–32. Ties: lowest cost, then axis x < y < z, then lowest bin. Stable partition. Degenerate centroid bounds → object median. Node allocation must not depend on thread scheduling (build subtrees into task-local arrays, then concatenate in DFS order). |

### LBVH layout conversion (part of `layout`)

Karras produces `N-1` internal nodes and `N` single-primitive leaves, with explicit child references and no sibling adjacency. Each internal node covers a contiguous sorted range `[first, last]`. Conversion:

1. Compute subtree primitive counts during refit.
2. Collapse: an internal node with range size ≤ `max_leaf_size` becomes a leaf (`left_or_first = first`, `count = size`).
3. Compute output node counts per subtree after collapse (bottom-up).
4. Assign output slots top-down: a node's two children get the adjacent pair `(c, c+1)`. The left child's descendants follow, then the right child's. This uses only subtree sizes, so it parallelizes with OpenMP tasks and is deterministic.
5. `prim_refs` = refs in sorted Morton order.

Alternative considered: a layout with explicit left and right indices, which Karras could fill directly. Rejected because median and SAH already produce adjacent pairs, and the conversion also performs leaf collapse.

### Depth cap (all methods)

- With 30-bit codes and index tie-breaks, LBVH depth is at most 30 + ceil(log2 N) + 1 (51 at 1M primitives). The validator still checks it.
- **Recovery rule:** during finalize, any node at depth `RT_FALLBACK_DEPTH` (32) whose subtree is deeper than 32 has that subtree rebuilt with the median method. Median depth ≤ ceil(log2 count) ≤ 32, so the total stays ≤ 64. `stats.fallbacks` counts these rebuilds and is reported.
- The 64-entry traversal stack is then always sufficient.

### Edge cases

| Input | Result |
|---|---|
| 0 primitives | `nodes` empty, `num_nodes = 0`. Traversal returns a miss without reading memory. |
| 1 primitive | Root is a leaf with `count = 1`. |
| All centroids equal | Median and LBVH split by canonical order. SAH falls back to object median. |
| Zero-area primitives | Kept. A degenerate triangle never reports a hit, but stays in the tree. |

### Determinism and validation

- The same input and parameters produce byte-identical `nodes` and `prim_refs` for **any thread count** on the same machine.
- The validator checks: every primitive appears exactly once; child bounds lie within the parent's; leaf bounds contain their primitives; no empty boxes; depth ≤ 64; `count ≤ max_leaf_size` except fallback leaves.
- `sah_cost` is a reported diagnostic, not a pass/fail test. SAH is not guaranteed to beat LBVH on every scene.

## 4. Camera

Owner: Jett.

```cpp
struct Camera { vec3 position, look_at, up; float vfov_deg; uint32_t width, height; };
RT_HOSTDEV Ray camera_ray(const Camera&, uint32_t px, uint32_t py);
```

- Pixel `(px, py)`, with `py = 0` the top row, maps to a ray through the pixel center `(px + 0.5, py + 0.5)`.
- `dir` is normalized, `tmin = 0`, `tmax = +inf`. Because `dir` is normalized, `t` is a distance.
- `aspect = width / height`. Basis: `w = normalize(position - look_at)`, `u = normalize(cross(up, w))`, `v = cross(w, u)`.

## 5. Intersection and traversal

Owner: Mikail.

```cpp
struct Hit { float t; PrimRef prim; float u, v; };   // 16 B; miss: prim = 0xFFFFFFFF, t = ray.tmax

RT_HOSTDEV bool intersect_sphere  (const Ray&, const Sphere&,   float t_hi, float& t);
RT_HOSTDEV bool intersect_triangle(const Ray&, const Triangle&, float t_hi, float& t, float& u, float& v);
RT_HOSTDEV Hit  traverse_closest(const Ray&, const Scene&, const BvhView&);
RT_HOSTDEV bool traverse_any    (const Ray&, const Scene&, const BvhView&);
RT_HOSTDEV Hit  brute_closest(const Ray&, const Scene&);    // test oracle and early stub
RT_HOSTDEV bool brute_any    (const Ray&, const Scene&);
```

### Frozen rules

| Rule | Decision |
|---|---|
| Valid interval | Open: a hit counts iff `ray.tmin < t < ray.tmax`. |
| Closest-hit acceptance | Accept candidate `(t, p)` iff it's in the interval and (no hit yet, or `t < best.t`, or `t == best.t && p < best.prim`). Equal distances resolve to the lowest `PrimRef`, independent of traversal order. |
| `t_hi` argument | Primitive tests report any root with `tmin < t <= t_hi`. The caller passes `t_hi = best.t` once it has a hit (so ties reach the acceptance rule), and `ray.tmax` before that, rejecting `t == ray.tmax`. |
| Node culling | Visit a node iff `intersect_aabb(..., ray.tmin, best_t_or_tmax, t_enter)` is true. The far bound is **inclusive**, so ties are never culled. |
| Child order | Visit the child with the smaller `t_enter` first. Equal `t_enter` → left first. This affects only speed, not the result. |
| Any-hit | Returns true iff any primitive has `tmin < t < tmax`. Order-independent. |
| Triangle test | **Watertight** (Woop et al. 2013, PBRT v4 Sec. 6.5.3). Two-sided: no backface culling for primary or shadow rays. Edges and vertices inclusive. Shared edges may report both triangles at the same `t`; the tie-break picks one. |
| Degenerate triangle | Zero determinant → miss. |
| Sphere test | Stable quadratic: `q = -0.5 (b + copysign(sqrt(disc), b))`, roots `q/a` and `c/q`. Take the smallest root in the interval. An origin inside the sphere returns the far root. |
| Traversal stack | Fixed `RT_MAX_BVH_DEPTH` entries. No recursion. `num_nodes == 0` → miss. |

`brute_closest` / `brute_any` use the same acceptance rule, so BVH and brute-force results must be identical, not just close.

## 6. Shading

Owner: Arek.

```cpp
struct SurfacePoint { vec3 p; vec3 ng; uint32_t material_id; };   // ng: geometric normal, unit, faces -ray.dir

RT_HOSTDEV SurfacePoint surface_at(const Ray&, const Hit&, const Scene&);
RT_HOSTDEV vec3  offset_ray_origin(vec3 p, vec3 ng);              // Ray Tracing Gems I, Ch. 6
RT_HOSTDEV bool  light_sample(const SurfacePoint&, vec3 wo, const Scene&, uint32_t i,
                              color& contrib, Ray& shadow_ray);

template <class Query>   // see "Tracing queries"
RT_HOSTDEV PixelResult shade_hit(const Ray&, const Hit&, const Scene&, const Query&);
```

### Tracing queries

Shading never calls a specific traversal. It's templated on a **query** type with two members, chosen at compile time (no virtual calls in device code):

```cpp
// Query concept:
//   RT_HOSTDEV Hit  closest(const Ray&) const;   // primary rays
//   RT_HOSTDEV bool any(const Ray&) const;       // shadow rays, uses ray.tmax

struct BvhQuery {                 // Core: our BVH (CPU backend, megakernel)
  Scene s; BvhView bvh;
  RT_INLINE Hit  closest(const Ray& r) const { return traverse_closest(r, s, bvh); }
  RT_INLINE bool any(const Ray& r) const     { return traverse_any(r, s, bvh); }
};

struct BruteQuery {               // Core: --bvh none (first image, tests). Loops over every primitive.
  Scene s;
  RT_INLINE Hit  closest(const Ray& r) const { return brute_closest(r, s); }
  RT_INLINE bool any(const Ray& r) const     { return brute_any(r, s); }
};

// Extension/Stretch, defined in their own modules:
//   EmbreeQuery (host only): rtcIntersect1 / rtcOccluded1, geomID+primID -> PrimRef
//   OptixQuery  (device only, used inside raygen): optixTrace with closest-hit / terminate-on-first-hit
```

- An empty BVH stays an empty scene (`num_nodes == 0` → miss). Brute force is a different query type, not a special BVH.
- `BvhQuery` and `BruteQuery` follow every frozen rule in [section 5](#5-intersection-and-traversal), so they return identical hits.
- External queries (Embree, OptiX) honor the open interval but may break exact-distance ties differently. Those cases show up as edge `prim_id` mismatches in `rt_compare`, which is acceptable in the best-possible tier.
- Backends select the query once on the host (`--bvh none` → `BruteQuery`, otherwise `BvhQuery`) and call the matching template instantiation of their pixel loop or kernel.

### Pixel result

Every backend produces one `PixelResult` per pixel. Color and both AOVs come from the same primary hit, so nothing gets retraced.

```cpp
struct PixelResult {           // 20 B
  color    rgb;                // linear, before clamping
  uint32_t prim_id;            // = hit.prim; 0xFFFFFFFF on miss
  uint32_t light_mask;         // contributing lights (see AOV semantics); 0 on miss
};

// Shared per-pixel entry point for every backend:
template <class Query>
RT_INLINE PixelResult render_pixel(const Camera& cam, uint32_t px, uint32_t py,
                                   const Scene& s, const Query& q) {
  Ray r = camera_ray(cam, px, py);
  Hit h = q.closest(r);
  return shade_hit(r, h, s, q);         // handles the miss case; shadow rays use q.any
}
```

- CPU backend: `render_pixel<BvhQuery>` or `render_pixel<BruteQuery>`. Megakernel: `render_pixel<BvhQuery>`, with `BruteQuery` also instantiable for small-scene debugging. Embree: `render_pixel<EmbreeQuery>`. OptiX: raygen calls `render_pixel<OptixQuery>`.
- Embree and OptiX map their IDs to `PrimRef` inside the query, so the AOVs stay comparable.
- The wavefront pipeline splits `shade_hit` into its light loop and an accumulate step, but writes the same three fields.
- Writing a `PixelResult` into `Image`: `rgb` always; `prim_id` and `light_mask` only when those AOVs are requested.

### Shadow-ray construction (frozen)

1. `p` = hit point, computed as `ray.origin + t * ray.dir`. For triangles, use the barycentric point instead (more accurate). `ng` is face-forwarded toward the viewer.
2. Point light: `to_l = L.pos - p`, `d2 = dot(to_l, to_l)`, `l = to_l / sqrt(d2)`.
3. If `dot(ng, l) <= 0`, the light is behind the surface: no contribution and no shadow ray. Return false.
4. Origin `o = offset_ray_origin(p, ng)`, which offsets toward the light's side.
5. Point light: `dir = normalize(L.pos - o)`, `tmin = 0`, `tmax = length(L.pos - o)`. Lights are not geometry, so the open interval is enough.
6. Directional light (Extension): `dir = normalize(-L.dir)`, `tmin = 0`, `tmax = +inf`.
7. The lighting terms use `l` and `d2` from step 2, computed from `p`, not from `o`.

### Shading model

Core: `contrib_i = albedo * max(0, dot(ng, l)) * I_i / d2`.

Extension (`MAT_SPECULAR`): adds `ks * max(0, dot(ng, h))^shininess * I_i / d2`, where `h = normalize(l + wo)`.

### Accumulation order (frozen)

```
miss:  rgb = scene.background; prim_id = 0xFFFFFFFF; light_mask = 0
hit:   rgb = albedo * ambient; prim_id = hit.prim; light_mask = 0
       for i = 0 .. L-1 in order:
           if light_sample(...) and !q.any(shadow_ray):
               rgb += contrib_i
               light_mask |= (1u << i)        // only when i < 32
```

Every backend (CPU, megakernel, wavefront, Embree, OptiX) uses this exact order. The wavefront pipeline writes per-(pixel, light) contributions and occlusion flags to fixed slots. Its accumulate kernel then sums them in light order. **No floating-point atomics on color**, so repeated runs are bit-identical.

## 7. Backend interface

Owner: Mikail.

```cpp
enum AovFlags : uint32_t { AOV_PRIM_ID = 1u << 0, AOV_LIGHT_MASK = 1u << 1 };

struct RenderSettings {        // effective values, after JSON + CLI overrides
  uint32_t width, height;      // fixed for the lifetime of one preparation
  uint32_t aovs;               // AovFlags
};

struct PrepareInput {
  const SceneHost* scene;      // must outlive the backend
  const Bvh*       bvh;        // nullptr if !needs_bvh()
  RenderSettings   render;
  int  build_threads, render_threads;
  MemConfig mem;               // mode, advice, prefetch (CUDA only)
};

class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool needs_bvh() const = 0;       // cpu, cuda: true; embree, optix: false
  virtual void prepare(const PrepareInput&, Timings&) = 0;   // releases any previous preparation first
  virtual void render(const Camera&, Image& out, Timings&) = 0;  // repeatable; out is host memory on return
  virtual void release() = 0;               // frees device/library resources; idempotent
  virtual const char* name() const = 0;
};
```

- `rt_render` resolves the effective camera and `RenderSettings` (JSON values, then CLI overrides) **before** preparation. It passes the settings to `prepare` and the camera to `render`.
- `rt_render` builds our BVH only when `needs_bvh()` is true **and** `--bvh` isn't `none`. With `--bvh none`, `PrepareInput::bvh` is null and the CPU/CUDA backends instantiate their loops with `BruteQuery`. The CUDA backend uploads the BVH, when present, through its `DeviceScene`. Embree and OptiX build their own structures inside `prepare`, timed as `convert`.
- `prepare` allocates every per-frame buffer (framebuffer, requested AOV buffers, wavefront queues), timed as `alloc`. `render` never allocates device memory, so the T_frame timer contains only `render` and `download`.
- `render` requires `cam.width/height` to equal the prepared `width/height`, and fails otherwise. **A resolution or AOV change requires calling `prepare` again.** Camera position, target, up vector, and field of view may change freely between `render` calls.
- `render` returns only after the image and requested AOVs are in host memory.
- `prepare` can be called repeatedly. Each call fully releases the previous state first (untimed), so repetitions measure a full preparation.

## 8. Timing and results schema

Owner: Arek. Definitions and protocol: [BENCHMARKING.md](BENCHMARKING.md#timing-quantities).

### Components

| Component | Phase | Notes |
|---|---|---|
| `args`, `load`, `cuda_init` | Startup | Only in application time |
| `bounds`, `build`, `layout` | Preparation (CPU) | Our BVH backends |
| `convert` | Preparation | Embree/OptiX internal build |
| `alloc`, `upload` | Preparation (GPU) | Device allocations and copies |
| `render`, `download` | Frame | CPU backend has no `download` |
| `write` | Excluded | PNG/PFM output |

Components are host wall-clock intervals (`steady_clock`) with `cudaDeviceSynchronize` at stage boundaries. Kernel-only `cudaEvent` times are recorded as extra diagnostics (`render_kernel`). **Totals are measured directly with their own timer.** `unattributed = total − Σ components` is reported, not assumed to be zero.

### Results file (one JSON object per process)

```json
{
  "schema_version": 2,
  "machine": { "host": "", "cpu_model": "", "cpu_cores": 0, "gpu_name": "", "gpu_uuid": "",
               "driver": "", "cuda_runtime": "", "nvcc": "", "gcc": "", "os": "" },
  "build": { "commit": "", "dirty": false, "cmake_flags": "" },
  "scene": { "json": "", "hash": "0x...", "generator_command": "",
             "num_spheres": 0, "num_triangles": 0, "num_primitives": 0, "num_lights": 0 },
  "config": { "backend": "cuda", "pipeline": "mega", "bvh": "lbvh", "max_leaf_size": 4,
              "build_threads": 72, "render_threads": 72, "width": 1920, "height": 1080,
              "aovs": ["primid", "lightmask"],
              "camera": { "position": [0, 0, 0], "look_at": [0, 0, 0], "up": [0, 1, 0], "vfov_deg": 45,
                          "overridden": ["position"] },
              "query": "bvh",
              "mem": { "mode": "explicit", "advice": "none", "prefetch": false, "realloc_per_prepare": true,
                       "geometry_reused": false, "fresh_pages": null } },
  "app": { "total_ms": 0.0, "components": {}, "unattributed_ms": 0.0 },
  "build_and_render": { "runs": [ { "total_ms": 0.0, "components": {}, "unattributed_ms": 0.0 } ],
                        "median_ms": 0.0, "min_ms": 0.0, "max_ms": 0.0, "spread": 0.0,
                        "median_run_index": 0 },
  "frame": { "runs": [ { "total_ms": 0.0, "components": {} } ],
             "median_ms": 0.0, "min_ms": 0.0, "max_ms": 0.0, "spread": 0.0 },
  "bvh_stats": { "nodes": 0, "leaves": 0, "depth": 0, "fallbacks": 0, "sah_cost": 0.0 },
  "image": { "path": "", "rgb_hash": "0x..." }
}
```

- `spread = (max − min) / median`.
- `median_run_index` identifies the run whose components explain the median total. Medians of individual components are not summed.

## Contract-change process

1. Open an issue labeled `contract` stating the change and who it affects.
2. A single PR edits this file and the code together.
3. The PR author signs off in the description. **All three other members must approve the review** (GitHub doesn't let authors approve their own PRs).
4. Bump the JSON / `.bin` / results `version` field when a format changes.

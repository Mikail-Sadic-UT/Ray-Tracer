# Contract notes

Places where code follows [INTERFACES.md](INTERFACES.md) but the contract itself may need to change later. INTERFACES.md is a working agreement, expected to change during development. These notes record where it is thin or may need adjusting, so the gap is found again when it matters instead of rediscovered.

These are not bugs. A bug in someone's code is a GitHub issue for its owner.

## How to use

- **Add an entry** when a review or implementation finds a gap: code follows the contract, but the contract is incomplete or may be suboptimal. Use the next free ID.
- **Act when the trigger happens.** A change to a type, signature, or numeric rule follows [Interface changes](INTERFACES.md#interface-changes). A clarification needs only the normal review.
- **Delete the entry** in the commit that resolves it. The weekly meeting skims this list alongside the [stub register](ROADMAP.md#stub-register).

## Open notes

| ID | Contract | Gap | Trigger | Owners |
|---|---|---|---|---|
| W1 | §2 `Light` | `pad = 0.0f` makes `Light` non-trivial | A kernel stages lights in shared memory | Po, Mikail |
| W3 | §2 Valid input | Core builds accept specular materials but reject directional lights | E2 (Blinn-Phong) starts | Po, Arek |
| W6 | §5 Frozen rules, triangle test | The row cites PBRT v4 Sec. 6.5.3 but doesn't say which parts apply. `intersect_triangle` departs from PBRT in three ways (below); the open interval and `t_hi` rules require the last two | C4 shadow tests, or the next edit of §5 | Mikail, Arek |
| W8 | §5 Stack overflow, §6 `BvhQuery` | §5 requires an overflow flag that the backend checks after `render`, but no signature carries it. `traverse_*` take it as an extra defaulted `uint32_t* overflow = nullptr`; `BvhQuery` has no member for it, so traversal through the query stops silently on overflow. `CpuBackend` uses a private query that passes the flag | The megakernel needs the flag (C10), or a second backend copies the private query | Mikail, Po |
| W9 | §7 `MemConfig` | §7 names `MemConfig mem; // mode, advice, prefetch` but never defines it. `backend.hpp` defines `MemMode`, `MemAdvice` and `bool prefetch` from the ARCHITECTURE `--mem`, `--mem-advice` and `--prefetch` values, all defaulting to explicit / none / off | E1 memory modes start, or the next edit of §7 | Mikail |
| W10 | §8 Results file | The example JSON and the prose leave the shape of a results file open in several places; `rt_bench` made the choices listed below | A second reader of the file (plot scripts, Mikail's sweep scripts) depends on them, or the next edit of §8 | Arek, Mikail |
| W11 | §8 `scene` | `scene.json` and `scene.generator_command` have no source: `SceneHost` keeps neither the JSON path nor the generator metadata that `rt_gen` writes | `rt_render --results` lands | Mikail, Po |

### W1: `Light::pad` default initializer

The default member initializer makes `Light` not trivially default-constructible. nvcc 13.3 accepts `__shared__ rt::Light tile[32];` with warning #20054-D (dynamic initialization is not supported for `__shared__`) and ignores the initializer there, so `pad` isn't guaranteed to be zero in shared memory.

No current impact. The megakernel reads lights from global memory, uploads copy the host bytes (where `pad` is zero), and `pad` is never hashed.

**If triggered:** drop `= 0.0f`, add `static_assert(std::is_trivial<Light>::value)`, and construct lights with `Light{}` or explicit braces. `scene_io` already rejects a non-zero `pad`, so an uninitialized one fails loudly at load. This is a type change: interface PR, Po and Mikail approve.

### W3: specular materials in Core builds

§2 accepts directional lights only in builds with E2, but says nothing about `"specular": true` (`MAT_SPECULAR`). The loader accepts specular materials, and Core shading ignores `ks`, so a scene written for E2 renders as Lambert on a Core build without a warning.

**If triggered:** gate `MAT_SPECULAR` the same way as directional lights, or state that Core ignores it.

### W6: what "PBRT v4 Sec. 6.5.3" covers in the triangle test

§5 says the triangle test is watertight per Woop et al. 2013 and PBRT v4 Sec. 6.5.3, including the `double` edge-function fallback. `intersect_triangle` (#21) follows that, with three departures documented in its header:

- **Explicit `fmaf`** in the shear and the scaled distance, so a shared vertex transforms to the same bits on every compiler, CPU and GPU. `test_trace_device` checks triangle `t`, `u`, `v` bit for bit against the host.
- **Exact interval test on the returned `t`**, not PBRT's test on the scaled distance, which can reject by one ulp a distance it just returned. That would break the `t_hi = best.t` re-report that the tie rule depends on.
- **No conservative `deltaT` rejection.** §5 defines a hit by the open interval `(tmin, tmax)` alone, and self-intersection is handled by `offset_ray_origin` (§6 "Shadow-ray construction", step 4), so rejecting `t <= deltaT` would make brute force disagree with the contract.

No current impact: every rule in §5 is met, and these are the readings the rest of §5 forces.

**If triggered:** if C4 shadow tests show self-shadowing acne on large or distant triangles, fix it in `offset_ray_origin` or the shadow-ray setup, not by reinstating `deltaT` in the primitive test. Either way, add one sentence to the §5 triangle row naming these departures. That is a clarification: normal single review, heads-up to Arek.

### W8: where the traversal overflow flag lives
§5 "Stack overflow" says the CPU sets an overflow flag in release builds, the GPU sets a device-global flag with `atomicOr`, and the backend checks it after `render` and fails the run. It doesn't say how the flag reaches the traversal. §5 gives `traverse_closest` / `traverse_any` three parameters, and §6 gives `BvhQuery` only `Scene s; BvhView bvh;`.
`trace/traverse.hpp` adds a trailing `uint32_t* overflow = nullptr` to both functions, so every call written against §5 still compiles. A non-null flag is set with `atomicOr` on the device and a relaxed atomic OR on the host, so all threads can share one. Debug host builds `assert` first. `BvhQuery` matches §6 and passes no flag, so through the query an overflowing ray stops and returns the best hit so far without any report.
`CpuBackend` (#47) keeps a private `CheckedBvhQuery` with the same two calls plus the flag pointer, shared by all render threads, and `render` throws `rt::Error` when it is set. So the CPU path meets §5 without changing §6. No other impact: a BVH that passes `validate_bvh` (depth <= 64) never overflows.
**If triggered:** add `uint32_t* overflow` to `BvhQuery` (null allowed) and forward it, and add the optional parameter to the §5 signatures. That changes a type and two signatures, so it is an interface PR; Po (CPU backend) and Mikail approve. The backend owns the flag: one host `uint32_t` per `render` on the CPU, and one device word allocated in `prepare` on the GPU.
### W9: `MemConfig` fields
§7 gives `PrepareInput` a `MemConfig mem` member described only as "mode, advice, prefetch (CUDA only)". `backend.hpp` (#47) defines it from the ARCHITECTURE CLI values: `MemMode { Explicit, Managed, System, SystemReuse }`, `MemAdvice { None, PreferredGpu, ReadMostly }`, and `bool prefetch`. The defaults are explicit, none, and off. `rt_render` doesn't parse `--mem` yet: only `explicit` (Core) exists, and the CUDA backend hasn't landed.
**If triggered:** copy the definition into §7 as a clarification (normal single review), and add `--mem`, `--mem-advice` and `--prefetch` to `rt_render` with the E1 work.
### W10: shape of the results file

§8 gives an example object and a few rules. `rt_bench` (`bench/results.hpp`, #50) fills the gaps like this, and `validate_results` enforces it:

- Only the sections a process measured are written: `app` in app mode, `build_and_render` and `frame` in prepared mode. `bvh_stats` is omitted when no BVH was built.
- Every component of a run's quantity is written, with 0 for stages the backend does not use (a CPU run has `convert`, `alloc`, `upload` and `download` as 0). `render_kernel` appears only in `diagnostics`, and only when positive, so CPU runs have an empty `diagnostics`.
- The example's `frame` runs have no `unattributed_ms` and `frame` has no `median_run_index`. Both are written, since BENCHMARKING asks for `unattributed` everywhere and the plots use the median run.
- For an even number of runs the median is the mean of the two middle totals and `median_run_index` is the lower-middle run. `spread` is 0 when the median is 0.
- `image.rgb_hash` is FNV-1a 64 over the little-endian bits of the linear RGB floats (`hash_rgb`). `machine.driver` is the NVIDIA driver version from `nvidia-smi`, or the CUDA driver API version if that is missing.

**If triggered:** copy these rules into §8 and the example as a clarification (normal single review, Mikail reads). Changing a key or a rule is a format change: bump `schema_version` and follow [Interface changes](INTERFACES.md#interface-changes).

### W11: where `scene.json` and `scene.generator_command` come from

§8 records the scene JSON path and the exact `rt_gen` command, but `load_scene` returns a `SceneHost` that holds neither: the path is only known to `rt_render`, and the `generator` object that `rt_gen` writes (its `command` field) is dropped on load (`scene_io.hpp`). `rt_bench` takes both as plain strings in `SceneInfo`.

`rt_render` can pass its `--scene` argument as the path and read `generator.command` with a second small JSON parse of the same file, so no type has to change.

**If triggered:** if two callers need the command, add `std::string generator_command` to `SceneHost` or a `load_scene_info` helper in `scene_io`. That is a type change in Po's code and §2: interface PR, Po and Mikail approve.

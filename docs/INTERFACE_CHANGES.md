# Interface additions and changes

[INTERFACES.md](INTERFACES.md) is **frozen at v1.0** (Nov 22, after G2). New interface work is recorded here instead of editing that file. This keeps the original contract readable as it was agreed, and puts everything that has moved since in one place.

## How it works

- **Add an entry** below for any change to a type, signature, numeric rule, file format, or the results schema. Use the next free ID (`IC<n>`). Small clarifications that change none of those need no entry.
- **Say what it touches:** the INTERFACES section, the old behavior, the new behavior, why, and the PR. State whether it is *additive* (existing callers still compile and behave the same) or *breaking*.
- **Point from INTERFACES.md only when a reader would be misled.** Put a one-line note in the affected section, such as `> Changed: see IC3 in INTERFACE_CHANGES.md`. Don't rewrite the section.
- **Version:** the freeze is v1.0. Bump the minor version in the log below for each additive entry (v1.1, v1.2, ...), and the major version for a breaking one. File formats keep their own `version` fields, which are bumped as before.
- **Tests and code land in the same PR** as the entry, as before. CI and the golden/`rt_compare` checks decide whether a numeric change is acceptable. Prefer additive changes, because a breaking one touches every backend.
- **Not yet decided here:** nothing needs an owner's sign-off while one person is working on the project. If more people join, bring back the owner approval from [INTERFACES.md "Interface changes"](INTERFACES.md#interface-changes).

## Log

| ID | Version | Date | Section | Kind | Summary | PR / issue |
|---|---|---|---|---|---|---|
| IC1 | v1.0 | Nov 13 | General rules | Behavior | FMA contraction is off for all project code (`-ffp-contract=off`, nvcc `--fmad=false`). The wording change is already in INTERFACES.md's general rules. CPU and GPU images are now byte-identical on the known and benchmark scenes. Explicit `fmaf()` calls are unaffected | #65 |
| IC2 | v1.1 | Nov 22 | §6 `BvhQuery`, §5 `traverse_*` | Additive | `BvhQuery` gains `uint32_t* overflow = nullptr`, forwarded to `traverse_closest` / `traverse_any` (which already took the trailing pointer). `CpuBackend` and `CudaBackend` use `BvhQuery` and their private `CheckedBvhQuery` copies are gone. The backend still owns the flag. A two-member initializer keeps compiling and reports nothing, as before. Closes [CONTRACT_NOTES W8](CONTRACT_NOTES.md#w8-where-the-traversal-overflow-flag-lives) | #69 |
| IC3 | v1.2 | Nov 22 | §1 `AABB`, constants | Additive | `static AABB empty()` (host and device) returns `{{inf,inf,inf},{-inf,-inf,-inf}}`; the builder and generator use it. `RT_EPS` and `RT_GAMMA3` moved from `aabb.hpp` to `math.hpp`; `aabb.hpp` still includes `math.hpp`, so existing code compiles unchanged | #4 |
| IC4 | v1.3 | Nov 23 | §2 Valid input and JSON, §6 Shading model | Additive | E2 is on in every build (there is no separate E2 build). Loader: `{"type": "directional", "direction": [x, y, z], "intensity": [...]}` is accepted, where `direction` is the way the light travels; it must be within the coordinate bound and non-zero, is normalized at load, and `save_scene` writes it back under the same keys. Shading follows the §6 extension rules: `MAT_SPECULAR` adds `ks * max(0, dot(ng, h))^shininess * I / d2`, and a directional light uses `l = normalize(-dir)`, `d2 = 1` and an unbounded shadow ray. New numeric rule: the exponent is computed by `spec_pow` in `shading.hpp` (integer part by repeated squaring, 16 fractional bits by repeated `sqrtf`), not `powf`, so CPU and GPU stay byte-identical. Scenes without specular materials or directional lights render the same bytes as before; all goldens are unchanged. Closes [CONTRACT_NOTES W3](CONTRACT_NOTES.md#w3-specular-materials-in-core-builds) | `mikail/blinn-phong` |
| IC5 | v1.4 | Nov 23 | §2 material types and JSON, §6 Shading model and accumulation | Additive | `MAT_MIRROR = 1u << 1` (reserved since v1.0) is defined: the material reflects with reflectance `ks`. Scene JSON takes an optional `"mirror"` boolean per material (absent means false); `save_scene` writes it only when true. Shading: after the primary hit's direct light (unchanged, frozen order), a mirror follows up to `RT_MAX_MIRROR_BOUNCES = 4` reflection rays, each from `offset_ray_origin(p, ng)` along `normalize(d - 2 * dot(d, ng) * ng)`; the throughput is multiplied by each mirror's `ks`, every surface reached adds `throughput * direct light` (ambient plus lights in index order), and a miss adds `throughput * background`. `prim_id` and `light_mask` describe the primary hit only. The `Query` passed to `shade_hit` must now also provide `closest` (both existing queries do). Scenes without mirrors render the same bytes; CPU and GPU stay byte-identical with mirrors too | `mikail/mirror` |

## Pending (decided to add, not yet done)

These are not part of the frozen contract yet. Each becomes a log entry when it lands.

None.

## Open questions

Interface questions that came up but have no proposal yet go here, so they are not rediscovered. Other known gaps stay in [CONTRACT_NOTES.md](CONTRACT_NOTES.md).

- Results schema: the schema has no slot for builder sub-stage timings (#68). `RT_BVH_STAGES=1` prints them to stderr for LBVH builds ([BUILD.md](BUILD.md#runtime-diagnostics)), which has been enough so far. If the stages should appear in results files, that is a new entry (additive, new optional field).
- `--mem managed|system|system-reuse` (E1, if chosen): `MemConfig` is defined in `backend.hpp` but not in INTERFACES (W9). Defining it would be an entry here.

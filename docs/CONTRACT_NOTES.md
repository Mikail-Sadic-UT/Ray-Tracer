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
| W5 | §8 Components | `Timings::render_kernel` exists, but §8 doesn't say it overlaps `render` or where the results JSON reports it | The results writer lands (Nov 6), or `GpuTimer` (Nov 13) | Arek, Mikail, Po |

### W1: `Light::pad` default initializer

The default member initializer makes `Light` not trivially default-constructible. nvcc 13.3 accepts `__shared__ rt::Light tile[32];` with warning #20054-D (dynamic initialization is not supported for `__shared__`) and ignores the initializer there, so `pad` isn't guaranteed to be zero in shared memory.

No current impact. The megakernel reads lights from global memory, uploads copy the host bytes (where `pad` is zero), and `pad` is never hashed.

**If triggered:** drop `= 0.0f`, add `static_assert(std::is_trivial<Light>::value)`, and construct lights with `Light{}` or explicit braces. `scene_io` already rejects a non-zero `pad`, so an uninitialized one fails loudly at load. This is a type change: interface PR, Po and Mikail approve.

### W3: specular materials in Core builds

§2 accepts directional lights only in builds with E2, but says nothing about `"specular": true` (`MAT_SPECULAR`). The loader accepts specular materials, and Core shading ignores `ks`, so a scene written for E2 renders as Lambert on a Core build without a warning.

**If triggered:** gate `MAT_SPECULAR` the same way as directional lights, or state that Core ignores it.

### W5: `render_kernel` in the results schema

`Timings::render_kernel` (kernel-only `cudaEvent` time) landed in #22. It overlaps `render`, a host wall-clock interval around the same launch, so summing it with the components would double-count. §8 names it as a diagnostic but doesn't say where the results JSON reports it, or that it is excluded from `unattributed` and the totals.

**Proposed:** report it in a separate per-run `diagnostics` object, not inside `components`, and add one sentence to §8 saying it is excluded from `unattributed` and the totals. The name is already in §8, so this is a clarification: normal single review, heads-up to Po (the CPU backend leaves it at zero).

**If triggered:** Arek's results writer needs to know before it sums `components`.

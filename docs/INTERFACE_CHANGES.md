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

## Pending (decided to add, not yet done)

These are not part of the frozen contract yet. Each becomes a log entry when it lands.

| ID | Section | Proposal | Kind | Issue |
|---|---|---|---|---|
| IC2 | §6 `BvhQuery`, §5 `traverse_*` | Add `uint32_t* overflow = nullptr` to `BvhQuery` and forward it, so `CpuBackend` and `CudaBackend` can drop their private `CheckedBvhQuery` copies and an overflow through the public query is reported. Closes [CONTRACT_NOTES W8](CONTRACT_NOTES.md#w8-where-the-traversal-overflow-flag-lives). `traverse_*` already take the pointer | Additive | #69 |
| IC3 | §1 `AABB`, constants | `static AABB empty()` returning `{{inf,inf,inf},{-inf,-inf,-inf}}`; move `RT_EPS` / `RT_GAMMA3` from `aabb.hpp` to `math.hpp` (existing includes keep compiling) | Additive | #4 |

## Open questions

Interface questions that came up but have no proposal yet go here, so they are not rediscovered. Other known gaps stay in [CONTRACT_NOTES.md](CONTRACT_NOTES.md).

- Results schema: the schema has no slot for builder sub-stage timings, which #68 wants. A temporary stderr report is enough to start. If the stages should appear in results files, that is a new entry (additive, new optional field).
- `--mem managed|system|system-reuse` (E1, if chosen): `MemConfig` is defined in `backend.hpp` but not in INTERFACES (W9). Defining it would be an entry here.

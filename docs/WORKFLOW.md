# Workflow

Git branching, pull requests, CI, issues, and team routines.

## Branches

```mermaid
flowchart RL
  J["jett/lbvh"] -->|PR + review + CI| D[dev]
  M["mikail/megakernel"] -->|PR + review + CI| D
  A["arek/blinn-phong"] -->|PR + review + CI| D
  P["po/generator"] -->|PR + review + CI| D
  D -->|PR at check-in, tag checkin-N| MAIN[main]
```

| Branch | Protected | Receives | Rules |
|---|---|---|---|
| `main` | Yes | PRs from `dev` only, at check-ins | Tagged `checkin-1`, `checkin-2`, `checkin-3`, `final` |
| `dev` | Yes | PRs from feature branches | ≥ 1 approving review, CPU CI green, GPU checkbox for rendering changes |
| `<member>/<topic>` | No | Your commits | Branch from `dev`, keep short-lived |

Branch names: lowercase first name + topic, e.g. `jett/lbvh`, `mikail/mem-modes`, `arek/blinn-phong`, `po/generator`.

Day to day:

```bash
git switch dev && git pull
git switch -c jett/lbvh
# ... work, commit ...
git fetch origin && git rebase origin/dev     # or: git merge origin/dev
git push -u origin jett/lbvh
# open PR into dev
```

- Sync with `dev` at least every few days. Small PRs (< ~400 lines) merge faster.
- Don't force-push to `dev` or `main`. Force-pushing your own feature branch after a rebase is fine.
- Never commit generated `.bin` scenes, build directories, or large images.

## Pull requests

PR template contents (file text in [REPO_SKELETON.md](REPO_SKELETON.md#pr-template)):

- **Summary**: what and why.
- **Area**: bvh / gpu / shading / cpu / scene / infra / bench / docs.
- **Tests added or updated.**
- **CPU CI**: green (automatic).
- **GPU check** (required if rendering code changed): laptop and/or TACC, with `rt_compare` output vs CPU reference.
- **Performance impact**: before/after numbers if relevant.
- **Contract change?** y/n. If yes, needs all four approvals (see [INTERFACES.md](INTERFACES.md#contract-change-process)).

Review rules:

- At least one approval from someone other than the author. Prefer the owner of the area being touched.
- Reviewers check correctness, tests, and contract compliance. Style is handled by `.clang-format`.
- Respond to reviews within ~1 day. Don't let PRs sit.
- `dev` → `main` PRs at each check-in are reviewed by the whole team in the check-in prep meeting.

## CI

GitHub Actions, CPU-only build (no GPU runners). Owner: Mikail.

On every PR and push to `dev`/`main`:

1. Configure with `-DRT_ENABLE_CUDA=OFF`, build with GCC 13.
2. Run unit tests (`ctest`).
3. Render known-case scenes on CPU and compare with committed golden images via `rt_compare`.
4. `clang-format --dry-run --Werror` on changed files.

GPU checks run manually (laptop or TACC) and are recorded in the PR. Outline in [REPO_SKELETON.md](REPO_SKELETON.md#ci-workflow-outline).

## Issues

Every task in [ROADMAP.md](ROADMAP.md) becomes a GitHub issue, assigned to its owner and linked to a milestone (check-in 1/2/3, final).

| Label | Use |
|---|---|
| `area:bvh`, `area:gpu`, `area:shading`, `area:cpu`, `area:scene`, `area:infra`, `area:bench` | Code area |
| `core`, `extended`, `stretch` | Scope tier |
| `contract` | Touches [INTERFACES.md](INTERFACES.md) |
| `blocked` | Waiting on another issue (link it) |
| `bug` | Defect |

## Check-ins

Before each check-in:

1. Feature freeze on `dev` 1–2 days before.
2. Run the full test suite plus a GPU check on both machines.
3. PR `dev` → `main`, whole-team review.
4. Tag `main` (`checkin-N`). Tags are what instructors and the report reference.

## Routines

- **Weekly team meeting** (~30 min): each person reports done / next / blocked. Jett updates milestone status in [ROADMAP.md](ROADMAP.md).
- **Weekly contribution log**: each member adds one row per week to the log at the bottom of their [member file](members/). Link PRs and issues. This feeds the report's contribution section, as the proposal requires.
- **Blocked > 2 days?** Switch to the relevant stub from [ROADMAP.md](ROADMAP.md#stubs), label the issue `blocked`, and raise it in the team chat.

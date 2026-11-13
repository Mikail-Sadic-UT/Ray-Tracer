#!/usr/bin/env bash
# Regenerate the lit regression goldens in scenes/golden/ from the known scenes.
#
# Deliberate use only (scenes/golden/README.md): never regenerate to make a failing test pass.
# Requires a clean tracked tree and a renderer built from that exact clean commit, plus a
# reason that goes into PROVENANCE.txt and the commit message you write.
#
#   scripts/regen_goldens.sh --render build/apps/rt_render/rt_render --reason "why" [--out DIR]
#
# Each scene is rendered twice and the two renders must be byte-identical before anything is
# written. --out DIR writes somewhere else (to try the script without touching the repo).
set -euo pipefail

usage() { echo "usage: $0 --render RT_RENDER --reason TEXT [--out DIR]" >&2; exit 2; }

render="" reason="" out=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --render) render="${2:?}"; shift 2 ;;
    --reason) reason="${2:?}"; shift 2 ;;
    --out) out="${2:?}"; shift 2 ;;
    *) usage ;;
  esac
done
[[ -n "$render" && -n "$reason" ]] || usage
[[ -x "$render" ]] || { echo "error: $render is not an executable" >&2; exit 1; }

root="$(git rev-parse --show-toplevel)"
cd "$root"
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "error: tracked files have uncommitted changes; commit or stash them first" >&2
  exit 1
fi
commit="$(git rev-parse HEAD)"
golden_dir="${out:-scenes/golden}"

# Compiler and build type come from the CMake cache of the build that made rt_render.
build_dir="$(cd "$(dirname "$render")/../.." && pwd)"
cache="$build_dir/CMakeCache.txt"
compiler="unknown (no CMakeCache.txt at $build_dir)"
build_type="unknown"
if [[ -f "$cache" ]]; then
  cxx="$(sed -n 's/^CMAKE_CXX_COMPILER:[A-Z]*=//p' "$cache")"
  build_type="$(sed -n 's/^CMAKE_BUILD_TYPE:[A-Z]*=//p' "$cache")"
  compiler="$cxx: $("$cxx" --version 2>/dev/null | head -n 1)"
fi

scenes=(known_two_spheres known_single_sphere known_degenerate_triangle)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

render_scene() {  # render_scene NAME DIR
  "$render" --scene "scenes/known/$1.json" --backend cpu --bvh median --shade lit \
    --build-threads 1 --render-threads 1 --pfm "$2/$1.pfm" --aov primid,lightmask \
    --results "$2/$1.results.json"
  python3 - "$2/$1.results.json" "$commit" <<'PYTHON'
import json
import sys

with open(sys.argv[1]) as source:
    build = json.load(source)["build"]
if build.get("commit") != sys.argv[2] or build.get("dirty") is not False:
    sys.exit("error: renderer was not built from this exact clean commit; rebuild rt_render "
             "and retry (goldens were not written)")
PYTHON
}

for name in "${scenes[@]}"; do
  mkdir -p "$work/a" "$work/b"
  render_scene "$name" "$work/a"
  render_scene "$name" "$work/b"
  for suffix in .pfm .primid.u32img .lightmask.u32img; do
    cmp "$work/a/$name$suffix" "$work/b/$name$suffix" \
      || { echo "error: two renders of $name$suffix differ; not writing goldens" >&2; exit 1; }
  done
done

mkdir -p "$golden_dir"
for name in "${scenes[@]}"; do
  for suffix in .pfm .primid.u32img .lightmask.u32img; do
    cp "$work/a/$name$suffix" "$golden_dir/$name$suffix"
  done
done

{
  echo "commit: $commit"
  echo "renderer: rt_render --backend cpu --bvh median --shade lit --build-threads 1 --render-threads 1 --aov primid,lightmask"
  echo "compiler: $compiler"
  echo "build type: $build_type"
  echo "reason: $reason"
  echo "sha256:"
  (cd "$golden_dir" && sha256sum known_*.pfm known_*.u32img)
} > "$golden_dir/PROVENANCE.txt"

# Keep the hash in the README in step with PROVENANCE.txt.
if [[ -z "$out" && -f scenes/golden/README.md ]]; then
  sed -i "s/^Producing commit: .*/Producing commit: $commit/" scenes/golden/README.md
fi
echo "wrote $golden_dir (commit $commit)"

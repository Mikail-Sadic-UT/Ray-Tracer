#!/usr/bin/env bash
# G2 correctness checks on one machine (ROADMAP C10, C6), written for a Vista GH node but also
# runnable on the laptop:
#   1. ctest on the build;
#   2. the benchmark scenes load with the manifest's content and scene hashes;
#   3. CPU vs CUDA through rt_compare on the known scenes and the benchmark scenes, plus
#      byte equality and a repeated CUDA run.
#
#   verify.sh --build DIR --scenes DIR --out DIR [--sizes "bench_100k_s1 bench_1m_s1"] [--no-ctest]
#
# Prints a summary and writes it to OUT/summary.txt. Exits non-zero if any check fails.
set -uo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="" scenes="" out="" sizes="bench_10k_s1 bench_100k_s1 bench_1m_s1" ctest=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build) build="$2"; shift 2 ;;
    --scenes) scenes="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --sizes) sizes="$2"; shift 2 ;;
    --no-ctest) ctest=0; shift ;;
    *) echo "verify.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
if [[ -z "$build" || -z "$scenes" || -z "$out" ]]; then
  echo "usage: verify.sh --build DIR --scenes DIR --out DIR [--sizes LIST] [--no-ctest]" >&2
  exit 2
fi
render="$build/apps/rt_render/rt_render"
compare="$build/apps/rt_compare/rt_compare"
dump="$build/apps/scene_dump/scene_dump"
for bin in "$render" "$compare" "$dump"; do
  [[ -x "$bin" ]] || { echo "verify.sh: $bin not found; build first" >&2; exit 2; }
done
mkdir -p "$out"
summary="$out/summary.txt"
: > "$summary"
failures=0
note() { echo "$*" | tee -a "$summary"; }
fail() { note "FAIL $*"; failures=$((failures + 1)); }
pass() { note "PASS $*"; }
threads="$(nproc --all)"
export OMP_PROC_BIND=close OMP_PLACES=cores
note "verify: host $(hostname -f 2>/dev/null || hostname), commit $(git -C "$repo" rev-parse --short HEAD), $threads cores"
note "verify: GPU $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null || echo none)"

if [[ $ctest == 1 ]]; then
  if ctest --test-dir "$build" --output-on-failure > "$out/ctest.txt" 2>&1; then
    pass "ctest: $(grep -E 'tests passed' "$out/ctest.txt")"
  else
    fail "ctest: $(grep -E 'tests passed' "$out/ctest.txt"); see $out/ctest.txt"
  fi
fi

# Scene hashes against scenes/manifest.json (ROADMAP C6 "Load on Vista").
for s in $sizes; do
  json="$scenes/$s.json"
  if [[ ! -f "$json" ]]; then
    fail "scene $s: $json not found (bash scripts/fetch_scenes.sh scenes-v1)"
    continue
  fi
  if ! "$dump" --verify "$json" > "$out/$s.verify.txt" 2>&1; then
    fail "scene $s: scene_dump --verify failed; see $out/$s.verify.txt"
    continue
  fi
  if python3 - "$repo/scenes/manifest.json" "$s" "$out/$s.verify.txt" <<'PY'; then
import json, sys
manifest, name, dump = sys.argv[1:]
entry = next(e for e in json.load(open(manifest))['scenes'] if e['name'] == name)
fields = dict(line.split(': ', 1) for line in open(dump).read().splitlines() if ': ' in line)
ok = (fields.get('content_hash') == entry['content_hash'] and
      fields.get('scene_hash') == entry['scene_hash'] and fields.get('validation') == 'PASS')
sys.exit(0 if ok else 1)
PY
    pass "scene $s: hashes match the manifest"
  else
    fail "scene $s: hashes differ from the manifest; see $out/$s.verify.txt"
  fi
done

# CPU vs CUDA (G2: rt_compare passes; with contraction off the files are normally identical).
check_pair() {  # name json bvh
  local name="$1" json="$2" bvh="$3" tag="$1.$3"
  local common=(--scene "$json" --bvh "$bvh" --build-threads "$threads" --shade lit
                --aov primid,lightmask)
  if ! "$render" "${common[@]}" --backend cpu --render-threads "$threads" \
       --pfm "$out/$tag.cpu.pfm" 2> "$out/$tag.cpu.log" ||
     ! "$render" "${common[@]}" --backend cuda --pfm "$out/$tag.cuda.pfm" 2> "$out/$tag.cuda.log" ||
     ! "$render" "${common[@]}" --backend cuda --pfm "$out/$tag.cuda2.pfm" 2> "$out/$tag.cuda2.log"; then
    fail "$tag: rt_render failed; see $out/$tag.*.log"
    return
  fi
  if "$compare" "$out/$tag.cpu.pfm" "$out/$tag.cuda.pfm" > "$out/$tag.compare.txt" 2>&1; then
    pass "$tag: rt_compare CPU vs CUDA ($(grep -c PASS "$out/$tag.compare.txt")/5 checks)"
  else
    fail "$tag: rt_compare CPU vs CUDA: $(grep FAIL "$out/$tag.compare.txt" | tr '\n' ';')"
  fi
  local same=1 repeat=1
  for ext in pfm primid.u32img lightmask.u32img; do
    cmp -s "$out/$tag.cpu.$ext" "$out/$tag.cuda.$ext" || same=0
    cmp -s "$out/$tag.cuda.$ext" "$out/$tag.cuda2.$ext" || repeat=0
  done
  if [[ $same == 1 ]]; then note "     $tag: CPU and CUDA files byte-identical"
  else note "     $tag: CPU and CUDA files differ (allowed if rt_compare passes)"; fi
  if [[ $repeat == 1 ]]; then pass "$tag: repeated CUDA run byte-identical"
  else fail "$tag: repeated CUDA runs differ"; fi
}
for k in known_two_spheres known_single_sphere known_degenerate_triangle; do
  check_pair "$k" "$repo/scenes/known/$k.json" median
done
for s in $sizes; do
  [[ -f "$scenes/$s.json" ]] && check_pair "$s" "$scenes/$s.json" lbvh
done
# The large images are only needed to diagnose a failure.
if [[ $failures == 0 ]]; then rm -f "$out"/*.pfm "$out"/*.u32img; fi
note "verify: $failures failure(s)"
[[ $failures == 0 ]]

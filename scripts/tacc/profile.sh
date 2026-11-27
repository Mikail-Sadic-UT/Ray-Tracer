#!/usr/bin/env bash
# GPU profiling for G3 and the report (BENCHMARKING.md "Profiling"), on a Vista GH node or the
# laptop:
#   1. register, stack and local-memory use of every kernel in rt_render (cuobjdump);
#   2. per scene, an Nsight Systems timeline of a prepared run (T_br and T_frame, 5 runs each)
#      and its kernel, memcpy and CUDA API summaries;
#   3. per scene, an Nsight Compute report (`--set full`) of one lit BVH megakernel launch.
#
#   profile.sh --build DIR --scenes DIR --out DIR [--sizes "bench_1m_s1"] [--build-threads B]
#              [--no-nsys] [--no-ncu] [--ncu-set SET]
#
# Writes OUT/summary.txt (the numbers worth quoting) next to the full reports: resources.txt,
# nsys_<scene>.nsys-rep and .stats.txt, ncu_<scene>.ncu-rep, .txt (details page) and .raw.csv.
# Open the .nsys-rep and .ncu-rep files in nsys-ui and ncu-ui. Timings taken under a profiler
# are not benchmark numbers: Nsight Compute replays the kernel and locks clocks to base, and
# Nsight Systems adds tracing overhead. Exits non-zero if any step fails; later steps still run.
set -uo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="" scenes="" out="" sizes="bench_10k_s1 bench_100k_s1 bench_1m_s1"
threads="$(nproc --all)" nsys=1 ncu=1 ncu_set=full
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build) build="$2"; shift 2 ;;
    --scenes) scenes="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --sizes) sizes="$2"; shift 2 ;;
    --build-threads) threads="$2"; shift 2 ;;
    --no-nsys) nsys=0; shift ;;
    --no-ncu) ncu=0; shift ;;
    --ncu-set) ncu_set="$2"; shift 2 ;;
    *) echo "profile.sh: unknown argument $1" >&2; exit 2 ;;
  esac
done
if [[ -z "$build" || -z "$scenes" || -z "$out" ]]; then
  echo "usage: profile.sh --build DIR --scenes DIR --out DIR [--sizes LIST] [--build-threads B]" \
       "[--no-nsys] [--no-ncu] [--ncu-set SET]" >&2
  exit 2
fi
render="$build/apps/rt_render/rt_render"
[[ -x "$render" ]] || { echo "profile.sh: $render not found; build first" >&2; exit 2; }
grep -q 'RT_ENABLE_CUDA:BOOL=ON' "$build/CMakeCache.txt" 2>/dev/null ||
  { echo "profile.sh: $build is not a CUDA build (RT_ENABLE_CUDA=ON)" >&2; exit 2; }
mkdir -p "$out"
summary="$out/summary.txt"
: > "$summary"
failures=0
note() { echo "$*" | tee -a "$summary"; }
fail() { note "FAIL $*"; failures=$((failures + 1)); }
export OMP_PROC_BIND=close OMP_PLACES=cores

note "profile: host $(hostname -f 2>/dev/null || hostname), commit $(git -C "$repo" rev-parse --short HEAD)$(git -C "$repo" diff --quiet HEAD || echo ' (dirty)')"
note "profile: GPU $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null || echo none)"
note "profile: $(grep -E '^RT_CUDA_ARCH' "$build/CMakeCache.txt" | sed 's/:STRING//'), B = $threads, lit, lbvh, 1920x1080"
for tool in nsys ncu; do
  note "profile: $tool $($tool --version 2>/dev/null | tail -1 || echo 'not found')"
done

# 1. Static resource use. The traversal stack is dynamically indexed, so it lives in the stack
# frame (local memory). PLAN.md "Megakernel registers": the lit BVH kernel is at 128, the most
# that still fits two 256-thread blocks per SM.
if cuobjdump --dump-resource-usage "$render" 2>&1 | c++filt > "$out/resources.txt" &&
   grep -q 'REG:' "$out/resources.txt"; then
  note ""
  note "Kernel resources (cuobjdump --dump-resource-usage):"
  awk '/Function/ { name = $0; sub(/^.*megakernel</, "megakernel<", name); sub(/>\(.*$/, ">", name) }
       /REG:/ && name ~ /megakernel/ { printf "  %-40s %s %s %s\n", name, $1, $2, $5; name = "" }' \
    "$out/resources.txt" | tee -a "$summary"
else
  fail "cuobjdump: no resource usage; see $out/resources.txt"
fi

common_args() {  # scene
  echo --scene "$scenes/$1.json" --backend cuda --pipeline mega --bvh lbvh \
       --build-threads "$threads" --shade lit --width 1920 --height 1080
}

for s in $sizes; do
  if [[ ! -f "$scenes/$s.json" ]]; then
    fail "$s: $scenes/$s.json not found (bash scripts/fetch_scenes.sh scenes-v1)"
    continue
  fi
  read -r -a args <<< "$(common_args "$s")"
  note ""
  note "== $s"

  # 2. Timeline. CPU sampling is off: it needs perf events, which compute nodes may not allow,
  # and the harness already times every preparation stage.
  if [[ $nsys == 1 ]]; then
    rep="$out/nsys_$s"
    if nsys profile --trace cuda,nvtx,osrt --sample none --cpuctxsw none --force-overwrite true \
         -o "$rep" "$render" "${args[@]}" --mode prepared --runs 5 \
         --results "$rep.results.json" > "$rep.log" 2>&1 &&
       nsys stats --force-export true --report cuda_gpu_kern_sum --report cuda_gpu_mem_time_sum \
         --report cuda_api_sum "$rep.nsys-rep" > "$rep.stats.txt" 2>&1; then
      note "nsys: kernels (time %, total ns, instances, avg ns, ..., name):"
      grep -E 'megakernel' "$rep.stats.txt" | head -3 | sed 's/^/  /' | cut -c1-160 | tee -a "$summary"
      note "nsys: memcpy and memset:"
      grep -E '\[CUDA (memcpy|memset)' "$rep.stats.txt" | sed 's/^/  /' | cut -c1-160 | tee -a "$summary"
    else
      fail "$s: nsys; see $rep.log and $rep.stats.txt"
    fi
  fi

  # 3. Kernel metrics of the single launch in an app-mode run. --set full replays the kernel
  # about 40 times. A permission error (ERR_NVGPUCTRPERM) means the node does not expose the
  # GPU performance counters to users.
  if [[ $ncu == 1 ]]; then
    rep="$out/ncu_$s"
    if ncu --set "$ncu_set" -k regex:megakernel -c 1 -f -o "$rep" \
         "$render" "${args[@]}" --mode app > "$rep.log" 2>&1 &&
       ncu --import "$rep.ncu-rep" --page details > "$rep.txt" 2>&1 &&
       ncu --import "$rep.ncu-rep" --page raw --csv > "$rep.raw.csv" 2>&1; then
      note "ncu ($ncu_set, clocks locked to base; times are not benchmark numbers):"
      for metric in 'Duration' 'Registers Per Thread' 'Theoretical Occupancy' \
                    'Achieved Occupancy' 'Block Limit Registers' 'Compute (SM) Throughput' \
                    'Memory Throughput' 'DRAM Throughput' 'L1/TEX Hit Rate' 'L2 Hit Rate' \
                    'Warp Cycles Per Issued Instruction' 'Avg. Active Threads Per Warp' \
                    'Local Memory Spilling Requests'; do
        grep -m1 -E "^ *$(printf '%s' "$metric" | sed 's/[().\/]/\\&/g') +" "$rep.txt" |
          sed -E 's/^ +/  /; s/ {2,}/  /g' | tee -a "$summary"
      done
    else
      fail "$s: ncu; see $rep.log$(grep -q ERR_NVGPUCTRPERM "$rep.log" 2>/dev/null && echo ' (no access to GPU performance counters)')"
    fi
  fi
done

note ""
note "profile: $failures failure(s); reports in $out"
[[ $failures == 0 ]]

#!/bin/bash
# Vista batch job: verify the build on the allocated node, then run one sweep preset.
#
#   sbatch scripts/tacc/sweep.sbatch                          # PRESET=g2 on gh-dev, 30 min
#   sbatch -p gh -t 02:00:00 --export=ALL,PRESET=core scripts/tacc/sweep.sbatch
#   sbatch --export=ALL,PRESET=smoke scripts/tacc/sweep.sbatch # check a node and the scripts
#   sbatch --export=ALL,PRESET=verify scripts/tacc/sweep.sbatch # G2 correctness checks only
#   sbatch -p gh -t 02:00:00 --export=ALL,PRESET="core final profile" scripts/tacc/sweep.sbatch
#   sbatch --export=ALL,PRESET=final,FROM=$SCRATCH/rt-results/core-123/sweep.csv scripts/tacc/sweep.sbatch
#
# Submit from the repo root (logs/ must exist: mkdir -p logs). Inputs, all optional:
#   PRESET   sweep.py preset (g2, core, cpu-threads, gpu-build-threads, size, bvh, resolution,
#            lights, tapp, smoke), verify for verify.sh only, final for the interleaved final
#            runs (sweep.py final), profile for profile.sh, or extra for extra.py. Default g2.
#   VERIFY   1 (default) runs verify.sh before a sweep; 0 skips it.
#            PRESET may be a space-separated list ("core final profile"): one allocation runs
#            verify.sh once, then each step in order, each into its own OUT/<preset> directory.
#   FROM     final only: the sweep.csv to choose CPU* and GPU* from. Default: the sweep.csv of
#            the last sweep preset earlier in the same PRESET list.
#   ROUNDS   final only: interleaved rounds (default 5).
#   FRAMES   extra only: walkthrough frames (default 240). EXTRA_ONLY: comma-separated
#            extra.py --only list, e.g. terrain_10k,balls when the mesh scenes are not copied.
#   OUT      results directory. Default $SCRATCH/rt-results/<presets joined by ->-<job id>.
#   RT_BUILD build tree (default <repo>/build-vista); RT_SCENE_DEST scene directory
#            (default <repo>/scenes/generated, where fetch_scenes.sh puts scenes-v1).
# Log every job in the node-hour log: bash scripts/tacc/node_hours.sh <job id>.
#SBATCH -J rt-sweep
#SBATCH -p gh-dev
#SBATCH -N 1
#SBATCH -n 1
#SBATCH -t 00:30:00
#SBATCH -o logs/%x.%j.out
#SBATCH -A TRA25006
set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-.}"
source scripts/tacc/env.sh
PRESET="${PRESET:-g2}"
OUT="${OUT:-$SCRATCH/rt-results/${PRESET// /-}-${SLURM_JOB_ID:-manual}}"
mkdir -p "$OUT"
echo "rt-sweep: preset $PRESET, node $(hostname -f), commit $(git rev-parse --short HEAD), out $OUT"

if [[ "$PRESET" == verify || "${VERIFY:-1}" == 1 ]]; then
  bash scripts/tacc/verify.sh --build "$RT_BUILD" --scenes "$RT_SCENE_DEST" --out "$OUT/verify"
fi
if [[ "$PRESET" != verify ]]; then
  read -r -a presets <<< "$PRESET"
  from="${FROM:-}"
  status=0
  for p in "${presets[@]}"; do
    dest="$OUT"
    [[ ${#presets[@]} -gt 1 ]] && dest="$OUT/$p"
    case "$p" in
      final)
        if [[ -z "$from" ]]; then
          echo "rt-sweep: final needs FROM=<sweep.csv> or a sweep preset before it" >&2
          exit 2
        fi
        python3 scripts/tacc/sweep.py final --sweep "$from" --machine vista \
          --scenes "$RT_SCENE_DEST" --rt-render "$RT_BUILD/apps/rt_render/rt_render" \
          --rounds "${ROUNDS:-5}" --out "$dest" || status=1
        python3 scripts/tacc/ratios.py "$dest/final.csv" > "$dest/ratios.md" || status=1
        ;;
      profile)
        bash scripts/tacc/profile.sh --build "$RT_BUILD" --scenes "$RT_SCENE_DEST" \
          --out "$dest" || status=1
        ;;
      extra)
        # terrain_10k comes from `fetch_scenes.sh scenes-extra-v1` and the mesh scenes are copied
        # in from the laptop; none is generated here (INTERFACES §2 "Generator determinism").
        # extra.py names any that are missing.
        python3 scripts/tacc/extra.py run --machine vista \
          --scenes "$RT_SCENE_DEST:$RT_REPO/scenes/meshes:$RT_REPO/scenes/extra" \
          --rt-render "$RT_BUILD/apps/rt_render/rt_render" --frames "${FRAMES:-240}" \
          ${EXTRA_ONLY:+--only "$EXTRA_ONLY"} --out "$dest" || status=1
        ;;
      *)
        # A failed sweep point stops this sweep; the steps after it still run.
        python3 scripts/tacc/sweep.py run --preset "$p" --machine vista \
          --scenes "$RT_SCENE_DEST" --rt-render "$RT_BUILD/apps/rt_render/rt_render" \
          --out "$dest" || status=1
        python3 scripts/tacc/ratios.py "$dest/sweep.csv" > "$dest/ratios.md" || status=1
        [[ -z "${FROM:-}" ]] && from="$dest/sweep.csv"
        ;;
    esac
  done
  [[ $status == 0 ]] || { echo "rt-sweep: a step failed; see the log above" >&2; exit 1; }
fi
echo "rt-sweep: done; results in $OUT"

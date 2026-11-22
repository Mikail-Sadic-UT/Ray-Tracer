#!/bin/bash
# Vista batch job: verify the build on the allocated node, then run one sweep preset.
#
#   sbatch scripts/tacc/sweep.sbatch                          # PRESET=g2 on gh-dev, 30 min
#   sbatch -p gh -t 02:00:00 --export=ALL,PRESET=core scripts/tacc/sweep.sbatch
#   sbatch --export=ALL,PRESET=smoke scripts/tacc/sweep.sbatch # check a node and the scripts
#   sbatch --export=ALL,PRESET=verify scripts/tacc/sweep.sbatch # G2 correctness checks only
#
# Submit from the repo root (logs/ must exist: mkdir -p logs). Inputs, all optional:
#   PRESET   sweep.py preset (g2, core, cpu-threads, gpu-build-threads, size, bvh, resolution,
#            tapp, smoke), or verify for verify.sh only. Default g2.
#   VERIFY   1 (default) runs verify.sh before a sweep; 0 skips it.
#   OUT      results directory. Default $SCRATCH/rt-results/<preset>-<job id>.
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
OUT="${OUT:-$SCRATCH/rt-results/$PRESET-${SLURM_JOB_ID:-manual}}"
mkdir -p "$OUT"
echo "rt-sweep: preset $PRESET, node $(hostname -f), commit $(git rev-parse --short HEAD), out $OUT"

if [[ "$PRESET" == verify || "${VERIFY:-1}" == 1 ]]; then
  bash scripts/tacc/verify.sh --build "$RT_BUILD" --scenes "$RT_SCENE_DEST" --out "$OUT/verify"
fi
if [[ "$PRESET" != verify ]]; then
  python3 scripts/tacc/sweep.py run --preset "$PRESET" --machine vista \
    --scenes "$RT_SCENE_DEST" --rt-render "$RT_BUILD/apps/rt_render/rt_render" --out "$OUT"
fi
echo "rt-sweep: done; results in $OUT"

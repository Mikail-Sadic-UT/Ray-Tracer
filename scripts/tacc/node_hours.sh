#!/usr/bin/env bash
# Prints node-hour log rows (BENCHMARKING.md "Node-hour budget") for finished Slurm jobs:
#   bash scripts/tacc/node_hours.sh <job id>... [--who NAME] [--purpose TEXT] [--total HOURS]
# --total is the running total before these jobs; each row adds its own node-hours to it.
# Paste the rows into the log table. Needs `sacct` (Vista login or compute node).
set -euo pipefail
who="${USER:-?}" purpose="" total=0 jobs=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --who) who="$2"; shift 2 ;;
    --purpose) purpose="$2"; shift 2 ;;
    --total) total="$2"; shift 2 ;;
    *) jobs+=("$1"); shift ;;
  esac
done
if [[ ${#jobs[@]} == 0 ]]; then
  echo "usage: node_hours.sh <job id>... [--who NAME] [--purpose TEXT] [--total HOURS]" >&2
  exit 2
fi
for job in "${jobs[@]}"; do
  # -X: the allocation line only, not its steps. ElapsedRaw is in seconds.
  line="$(sacct -j "$job" -X -n -P -o JobID,Partition,NNodes,ElapsedRaw,Start,JobName,State)"
  [[ -n "$line" ]] || { echo "node_hours.sh: no sacct record for job $job" >&2; exit 1; }
  IFS='|' read -r id queue nodes seconds start name state <<< "$line"
  read -r hours total <<< "$(python3 -c "h = $nodes * $seconds / 3600; print(f'{h:.2f}', f'{$total + h:.2f}')")"
  echo "| ${start%%T*} | $who | $queue | $id | $nodes × $(python3 -c "print(f'{$seconds/3600:.2f}')") = $hours | ${purpose:-$name ($state)} | $total |"
done

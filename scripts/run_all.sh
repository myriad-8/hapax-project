#!/usr/bin/env bash
# Full experiment matrix -> results/results.csv
# Tunables (env): DUR secs per run, REPS repeats, LOCKS list, CS_LIST, NCS_LIST, BURST_MS (0=off)
set -euo pipefail
cd "$(dirname "$0")/.."
DUR=${DUR:-3}; REPS=${REPS:-5}
LOCKS=${LOCKS:-"tas ticket pthread hapax_iw hapax_vw hapax_ad"}
CS_LIST=${CS_LIST:-"0 100 1000"}
NCS_LIST=${NCS_LIST:-"0 1000"}
BURST_MS=${BURST_MS:-0}
OUT=${OUT:-results/results.csv}
NCPU=$(nproc)
THREADS=$( { t=1; while [ $t -le $NCPU ]; do echo $t; t=$((t*2)); done; echo $NCPU; [ "${OVERSUB:-0}" = 1 ] && echo $((NCPU*2)); true; } | sort -nu | tr '\n' ' ')
echo "cores=$NCPU threads=[$THREADS] locks=[$LOCKS] reps=$REPS dur=${DUR}s"
./build/bench_tas -H > "$OUT"
total=$(( REPS * $(echo $CS_LIST | wc -w) * $(echo $NCS_LIST | wc -w) * $(echo $THREADS | wc -w) * $(echo $LOCKS | wc -w) ))
n=0
for rep in $(seq 1 $REPS); do
 for cs in $CS_LIST; do
  for ncs in $NCS_LIST; do
   for t in $THREADS; do
    for l in $LOCKS; do
      n=$((n+1)); printf '\r[%d/%d] rep=%d cs=%s ncs=%s t=%s %s      ' $n $total $rep $cs $ncs $t $l
      ./build/bench_$l -t $t -d $DUR -c $cs -n $ncs -p 1 -r $rep -B $BURST_MS >> "$OUT" || echo "FAILED: $l t=$t" >&2
    done
   done
  done
 done
done
echo; echo "done -> $OUT"

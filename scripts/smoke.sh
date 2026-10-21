#!/usr/bin/env bash
# 20-second sanity run of the whole pipeline
set -euo pipefail
cd "$(dirname "$0")/.."
make -s all
DUR=0.5 REPS=1 CS_LIST="0 100" NCS_LIST="0" OUT=results/smoke.csv ./scripts/run_all.sh
python3 scripts/plot.py results/smoke.csv

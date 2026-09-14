#!/usr/bin/env bash
# Aggregate hits and progress from all worker branches on origin (and the local worker).
cd "$(dirname "$0")/.."
git fetch -q origin '+refs/heads/*:refs/remotes/origin/*' 2>/dev/null
mkdir -p results/collected
: > results/collected/all_hits.txt
: > results/collected/all_summaries.txt
for ref in $(git for-each-ref --format='%(refname:short)' refs/remotes/origin/); do
  case "$ref" in
    origin/main) wid=0 ;;
    *worker-*) wid=$(echo "$ref" | sed -E 's/.*worker-([0-9]+)-.*/\1/') ;;
    *) continue ;;
  esac
  git show "$ref:results/worker_$wid/hits.txt" 2>/dev/null | sed "s|^|$ref |" >> results/collected/all_hits.txt
  { echo "== $ref"; git show "$ref:results/worker_$wid/summary.txt" 2>/dev/null; } >> results/collected/all_summaries.txt
done
echo "---- HITS ----"
sort -u results/collected/all_hits.txt | awk '{d=""; for(i=1;i<=NF;i++) if($i ~ /^digits=/) d=$i; print d, $0}' | sort -t= -k2 -n | cut -c1-300
echo "---- WORKERS ----"
grep -E "^(worker=)" results/collected/all_summaries.txt | awk '{print $1, $4, $5, $6}'
GEN=$(sed -n 's/.*GEN=\([0-9]*\).*/\1/p' control/plan.txt 2>/dev/null | head -1)
echo "---- TOTALS for current generation gen${GEN} (last STAT per process, from pushed summaries) ----"
grep -h "logs/gen${GEN}_proc.*STAT" results/collected/all_summaries.txt | sed -E 's/.*STAT cfgs=([0-9]+) .*k=([0-9]+) surv=([0-9]+).*prp8=([0-9]+) prp9=([0-9]+) nearmiss=([0-9]+) hits=([0-9]+) elapsed=([0-9]+)s rate=([0-9.e+]+).*/\1 \2 \4 \5 \6 \7 \9/' \
  | awk '{c+=$1; k+=$2; p8+=$3; p9+=$4; nm+=$5; h+=$6; r+=$7; n++} END {printf "processes=%d configs=%d total_k=%.4g prp8=%d prp9=%d nearmiss=%d hits=%d aggregate_rate=%.3g k/s\n", n, c, k, p8, p9, nm, h, r}'

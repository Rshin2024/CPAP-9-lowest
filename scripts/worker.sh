#!/usr/bin/env bash
# Search worker: runs one `search` process per core with the parameters given
# in control/plan.txt on origin/main, re-reading the plan every 2 minutes.
#   GEN     generation; when it changes the searches restart with the new params
#   P       largest prime of the modulus (0 = stop the worker)
#   KX      k range per offset, MAXUNC/BASEUNC covering thresholds, ITERS SA
#           iterations, VAR max variants per base covering, EXCL excluded primes
#   GNUM    total number of search processes (disjoint k ranges per process)
#   CODE    commit on origin/main whose src/ should be built and used
# Hits and progress are committed to results/worker_<id>/ and pushed to the
# current branch every 15 minutes (immediately on a hit).
#
# usage: scripts/worker.sh <worker_id> [max_hours]
set -u
WID=${1:?worker id required}
MAXH=${2:-12}
cd "$(dirname "$0")/.."
OUT=results/worker_$WID
mkdir -p "$OUT" logs
NP=$(nproc)
START=$(date +%s)
PIDS=()
LASTPUB=0
LASTHITS=0
CURGEN=""
CURCODE=""

git config user.email >/dev/null 2>&1 || git config user.email "cpap-worker@example.com"
git config user.name >/dev/null 2>&1 || git config user.name "cpap-worker-$WID"
BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null || true)
if [ -z "$BRANCH" ]; then
  BRANCH="cursor/cpap9-worker-$WID-68d6"
  git checkout -b "$BRANCH"
fi
echo "worker $WID on branch $BRANCH, $NP cores, max $MAXH h"

read_plan() {
  git fetch -q origin main 2>/dev/null || true
  local txt
  txt=$(git show origin/main:control/plan.txt 2>/dev/null || cat control/plan.txt)
  echo "$txt" | grep -oE '\b[A-Z]+=[-0-9a-f,]*' | tr '\n' ' '
}

stop_procs() {
  if [ ${#PIDS[@]} -gt 0 ]; then
    kill "${PIDS[@]}" 2>/dev/null
    wait "${PIDS[@]}" 2>/dev/null
  fi
  PIDS=()
}

update_code() {
  # build src/ from commit $CODE (must be reachable from origin/main)
  echo "$(date -u +%FT%TZ) updating code to $CODE"
  git fetch -q origin main 2>/dev/null || true
  if git checkout "$CODE" -- src scripts/verify.py 2>/dev/null; then
    if gcc -O3 -march=native -o bin/search.new src/search.c -lgmp -lm; then
      mv bin/search.new bin/search
      echo "rebuilt bin/search from $CODE"
    else
      echo "BUILD FAILED for $CODE, keeping old binary"
    fi
  else
    echo "could not check out $CODE"
  fi
}

start_procs() {
  for c in $(seq 0 $((NP - 1))); do
    local seed
    seed=$(( ( ($(od -An -N4 -tu4 /dev/urandom) & 0x7fffffff) * 1000 + WID * 10 + c ) ))
    local args=(-P "$P" -k 1 -K "$KX" -u "$MAXUNC" -i "$ITERS" -s "$seed" -o "$OUT/hits.txt" -B "${B:-262144}" -L "${L:-262144}")
    if [ -n "${EXCL:-}" ]; then args+=(-X "$EXCL"); fi
    if [ "${T:-9}" != "9" ]; then args+=(-T "$T"); fi
    if [ -n "${VAR:-}" ] && [ "${VAR:-0}" != "0" ]; then args+=(-v "$VAR"); fi
    if [ -n "${BASEUNC:-}" ]; then args+=(-w "$BASEUNC"); fi
    # global process index -> disjoint k ranges across all processes of all workers
    if [ -n "${GNUM:-}" ] && [ "${GNUM:-0}" != "0" ]; then args+=(-g $((WID * NP + c)) -G "$GNUM"); fi
    nohup ./bin/search "${args[@]}" >> "logs/gen${GEN}_proc${c}.log" 2>&1 &
    PIDS+=($!)
    echo "started proc $c pid $! seed $seed: ${args[*]}"
  done
}

publish() {
  {
    echo "worker=$WID branch=$BRANCH host=$(hostname) cores=$NP updated=$(date -u +%FT%TZ) elapsed_s=$(( $(date +%s) - START ))"
    echo "plan: GEN=${GEN:-} P=${P:-} KX=${KX:-} MAXUNC=${MAXUNC:-} BASEUNC=${BASEUNC:-} VAR=${VAR:-} ITERS=${ITERS:-} GNUM=${GNUM:-} EXCL=${EXCL:-} T=${T:-9} CODE=${CURCODE:-}"
    for f in logs/gen*_proc*.log; do
      [ -f "$f" ] || continue
      echo "$f: $(grep STAT "$f" | tail -n 1)"
    done
    echo "hits: $( [ -f "$OUT/hits.txt" ] && wc -l < "$OUT/hits.txt" || echo 0 )"
    grep -h NEARMISS logs/gen*_proc*.log 2>/dev/null | tail -n 20
  } > "$OUT/summary.txt"
  git add "$OUT" >/dev/null 2>&1
  if ! git diff --cached --quiet; then
    git commit -qm "worker $WID: progress $(date -u +%FT%TZ)" >/dev/null 2>&1
    for i in 1 2 3; do git push -q -u origin "$BRANCH" 2>/dev/null && break; sleep $((5 * i)); done
  fi
}

verify_hits() {
  python3 scripts/verify.py "$OUT/hits.txt" > "$OUT/verified.txt" 2>&1 || true
  cat "$OUT/verified.txt"
}

trap 'stop_procs; publish; exit 0' INT TERM

while true; do
  plan=$(read_plan)
  if [ -n "$plan" ]; then
    eval "$plan"
  fi
  if [ "${GEN:-}" != "$CURGEN" ] || [ "${CODE:-}" != "$CURCODE" ]; then
    echo "$(date -u +%FT%TZ) new plan: $plan"
    stop_procs
    if [ "${P:-0}" = "0" ]; then echo "plan says stop"; break; fi
    if [ -n "${CODE:-}" ] && [ "${CODE:-}" != "$CURCODE" ]; then update_code; fi
    CURGEN=${GEN:-}; CURCODE=${CODE:-}
    start_procs
  fi
  alive=()
  for pid in "${PIDS[@]}"; do if kill -0 "$pid" 2>/dev/null; then alive+=("$pid"); fi; done
  if [ ${#alive[@]} -lt ${#PIDS[@]} ] && [ "${P:-0}" != "0" ]; then
    echo "$(date -u +%FT%TZ) some search processes died (${#alive[@]}/${#PIDS[@]} alive), restarting all"
    stop_procs; start_procs
  fi
  now=$(date +%s)
  nh=$( [ -f "$OUT/hits.txt" ] && wc -l < "$OUT/hits.txt" || echo 0 )
  if [ "$nh" -gt "$LASTHITS" ]; then
    echo "$(date -u +%FT%TZ) NEW HIT(S):"; tail -n $((nh - LASTHITS)) "$OUT/hits.txt"
    LASTHITS=$nh
    verify_hits
    publish; LASTPUB=$now
  fi
  if [ $((now - LASTPUB)) -ge 900 ]; then publish; LASTPUB=$now; fi
  if [ $((now - START)) -ge $((MAXH * 3600)) ]; then echo "max hours reached"; break; fi
  sleep 120
done
stop_procs
publish
echo "worker $WID finished"

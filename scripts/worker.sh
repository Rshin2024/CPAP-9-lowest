#!/usr/bin/env bash
# Run immutable source snapshots selected by origin/main's plan.
# Usage: scripts/worker.sh <worker_id> [max_hours]
# CODE=main follows source changes; a full reachable commit hash pins the code.
# Every worker in a plan must use the same CPUS_PER_WORKER (default 4).
set -o pipefail

read_plan() {
  git fetch -q origin main || return 1
  git show origin/main:control/plan.txt
}

parse_plan() {
  local text=$1 line token key value seen=" " required
  local keys=() values=() tokens=()
  while IFS= read -r line || [ -n "$line" ]; do
    line=${line%%#*}
    read -r -a tokens <<< "$line"
    for token in "${tokens[@]}"; do
      [[ $token == *=* ]] || { echo "invalid plan token: $token" >&2; return 1; }
      key=${token%%=*}; value=${token#*=}
      [[ $seen != *" $key "* ]] || { echo "duplicate plan key: $key" >&2; return 1; }
      case "$key" in
        GEN|P|KX|MAXUNC|BASEUNC|VAR|ITERS|GNUM|T|B|L)
          [[ $value =~ ^(0|[1-9][0-9]*)$ && ${#value} -le 18 ]] || return 1 ;;
        EXCL) [[ -z $value || $value =~ ^[1-9][0-9]*(,[1-9][0-9]*)*$ ]] || return 1 ;;
        CODE) [[ $value == main || $value =~ ^[0-9a-f]{40}$ ]] || return 1 ;;
        *) echo "unknown plan key: $key" >&2; return 1 ;;
      esac
      seen+="$key "
      keys+=("$key"); values+=("$value")
    done
  done <<< "$text"
  for required in GEN P KX MAXUNC ITERS GNUM CODE; do
    [[ $seen == *" $required "* ]] || { echo "missing plan key: $required" >&2; return 1; }
  done
  EXCL=""; BASEUNC=""; VAR=0; GNUM=0; T=9; B=262144; L=262144
  for ((token=0; token<${#keys[@]}; token++)); do
    printf -v "${keys[token]}" '%s' "${values[token]}"
  done
  [[ $T =~ ^(8|9|10)$ && $KX -ge 2 && $B -ge 2 && $L -ge 1 ]] || return 1
  PLAN_TEXT="GEN=$GEN P=$P KX=$KX MAXUNC=$MAXUNC BASEUNC=$BASEUNC VAR=$VAR ITERS=$ITERS GNUM=$GNUM EXCL=$EXCL T=$T B=$B L=$L"
}

resolve_code() {
  local requested=$CODE
  [ "$requested" != main ] || requested=origin/main
  RESOLVED_CODE=$(git rev-parse --verify "$requested^{commit}") || return 1
  git merge-base --is-ancestor "$RESOLVED_CODE" origin/main || return 1
  SOURCE_TREE=$(git rev-parse --verify "$RESOLVED_CODE:src") || return 1
  VERIFIER_BLOB=$(git rev-parse --verify "$RESOLVED_CODE:scripts/verify.py") || return 1
  BUILDER_BLOB=$(git hash-object "$ROOT/scripts/build.sh") || return 1
  SOURCE_ID=$(printf '%s\n' "$SOURCE_TREE" "$VERIFIER_BLOB" "$BUILDER_BLOB" | git hash-object --stdin) || return 1
}

stop_procs() {
  if [ ${#PIDS[@]} -gt 0 ]; then
    kill "${PIDS[@]}" 2>/dev/null || true
    wait "${PIDS[@]}" 2>/dev/null || true
  fi
  PIDS=()
}

update_code() {
  local base="$ROOT/.releases/worker_$WID" stage destination digest
  mkdir -p "$base" || return 1
  stage=$(mktemp -d "$base/.build.XXXXXX") || return 1
  echo "$(date -u +%FT%TZ) building $RESOLVED_CODE"
  if ! git archive "$RESOLVED_CODE" src scripts/verify.py | tar -xf - -C "$stage"; then
    rm -rf "$stage"; return 1
  fi
  if ! cp "$ROOT/scripts/build.sh" "$stage/scripts/build.sh" ||
     [ "$(git hash-object "$stage/scripts/build.sh")" != "$BUILDER_BLOB" ] ||
     ! bash "$stage/scripts/build.sh" "$stage" "$stage/bin"; then
    echo "build failed for $RESOLVED_CODE" >&2
    rm -rf "$stage"; return 1
  fi
  digest=$("$PYTHON" -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$stage/bin/search") || { rm -rf "$stage"; return 1; }
  destination="$base/${RESOLVED_CODE}-$(date +%s)-$$"
  if [ -e "$destination" ] || ! mv "$stage" "$destination"; then
    rm -rf "$stage"; return 1
  fi
  # Advance provenance only after installing the complete, successful build.
  RUN_DIR=$destination
  CURCODE=$RESOLVED_CODE; CURSOURCE=$SOURCE_ID
  ACTIVE_SOURCE_TREE=$SOURCE_TREE; ACTIVE_VERIFIER=$VERIFIER_BLOB
  ACTIVE_BUILDER=$BUILDER_BLOB; BINARY_SHA256=$digest
}

start_procs() {
  if [ "$GNUM" -lt "$(( (WID + 1) * NP ))" ]; then
    echo "worker partition exceeds GNUM=$GNUM ($NP processes per worker)" >&2
    return 1
  fi
  if [ "$NP" != 4 ] && grep -q 'REMAP=' "$RUN_DIR/src/bases_$P.txt" 2>/dev/null; then
    echo "REMAP coverings require CPUS_PER_WORKER=4" >&2
    return 1
  fi
  local c seed
  for ((c=0; c<NP; c++)); do
    seed=$(( ( ($(od -An -N4 -tu4 /dev/urandom) & 0x7fffffff) * 1000 + WID * 10 + c ) ))
    local args=(-P "$P" -k 1 -K "$KX" -u "$MAXUNC" -i "$ITERS" -s "$seed" -o "$ROOT/$OUT/hits.txt" -B "$B" -L "$L" -T "$T")
    [ -z "$EXCL" ] || args+=(-X "$EXCL")
    [ "$VAR" = 0 ] || args+=(-v "$VAR")
    [ -z "$BASEUNC" ] || args+=(-w "$BASEUNC")
    [ "$GNUM" = 0 ] || args+=(-g "$((WID * NP + c))" -G "$GNUM")
    printf '%s GEN=%s process=%s CODE=%s source=%s binary_sha256=%s args=%s\n' \
      "$(date -u +%FT%TZ)" "$GEN" "$c" "$CURCODE" "$CURSOURCE" "$BINARY_SHA256" "${args[*]}" >> "$OUT/runs.txt"
    (cd "$RUN_DIR" && exec nohup env CPAP_WORKER_BRANCH="$BRANCH" ./bin/search "${args[@]}") >> "logs/gen${GEN}_proc${c}.log" 2>&1 &
    PIDS+=("$!")
  done
  ACTIVE_PLAN=$PLAN_TEXT
  RUN_STATUS=running
}

hit_count() {
  if [ -f "$OUT/hits.txt" ]; then wc -l < "$OUT/hits.txt"; else echo 0; fi
}

publish() {
  {
    echo "worker=$WID branch=$BRANCH host=$(hostname) cores=$NP updated=$(date -u +%FT%TZ) elapsed_s=$(( $(date +%s) - START )) status=${RUN_STATUS:-idle}"
    echo "active_plan: ${ACTIVE_PLAN:-none}"
    echo "requested_plan: ${PLAN_TEXT:-none} CODE=${CODE:-} resolved=${RESOLVED_CODE:-}"
    echo "code: CODE=${CURCODE:-none} source_tree=${ACTIVE_SOURCE_TREE:-none} verifier_blob=${ACTIVE_VERIFIER:-none} builder_blob=${ACTIVE_BUILDER:-none} binary_sha256=${BINARY_SHA256:-none}"
    echo "verification: ${VERIFICATION_STATUS:-not_run}"
    for f in logs/gen*_proc*.log; do
      [ -f "$f" ] || continue
      echo "$f: $(grep STAT "$f" | tail -n 1)"
    done
    echo "hits: $(hit_count)"
    grep -h NEARMISS logs/gen*_proc*.log 2>/dev/null | tail -n 20 || true
  } > "$OUT/summary.txt"
  git add -- "$OUT" || return 1
  if ! git diff --cached --quiet -- "$OUT"; then
    git commit -qm "worker $WID: progress $(date -u +%FT%TZ)" -- "$OUT" || return 1
    local i
    for i in 1 2 3; do
      git push -q -u origin "$BRANCH" && return 0
      sleep "$((5 * i))"
    done
    return 1
  fi
}

verify_hits() {
  local status=0
  "$PYTHON" "$RUN_DIR/scripts/verify.py" "$OUT/hits.txt" > "$OUT/verified.txt" 2>&1 || status=$?
  if [ "$status" = 0 ]; then VERIFICATION_STATUS="proved CODE=$CURCODE";
  else VERIFICATION_STATUS="failed exit=$status CODE=$CURCODE"; fi
  cat "$OUT/verified.txt"
  return "$status"
}

finish() {
  local status=$1
  stop_procs
  if [ -n "${RUN_DIR:-}" ] && [ "$(hit_count)" -gt "$LASTHITS" ]; then
    if verify_hits; then LASTHITS=$(hit_count);
    else RUN_STATUS=verification_failed; status=1; fi
  fi
  publish || status=1
  return "$status"
}

main() {
  WID=${1:?worker id required}; MAXH=${2:-12}
  [[ $WID =~ ^(0|[1-9][0-9]*)$ && ${#WID} -le 6 && $MAXH =~ ^[1-9][0-9]*$ && ${#MAXH} -le 6 ]] || return 1
  ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd) || return 1
  cd "$ROOT" || return 1
  PYTHON=${PYTHON:-$([ -x "$ROOT/.venv/bin/python" ] && echo "$ROOT/.venv/bin/python" || command -v python3)}
  PYTHON=$(command -v "$PYTHON") || return 1
  [[ $PYTHON == /* ]] || PYTHON="$ROOT/$PYTHON"
  OUT=results/worker_$WID
  mkdir -p "$OUT" logs || return 1
  NP=${CPUS_PER_WORKER:-4}
  [[ $NP =~ ^[1-9][0-9]*$ && ${#NP} -le 3 ]] || return 1
  START=$(date +%s); PIDS=(); LASTPUB=0; LASTHITS=0
  CURCODE=""; CURSOURCE=""; ACTIVE_PLAN=""; RUN_STATUS=idle
  git config user.email >/dev/null 2>&1 || git config user.email "cpap-worker@example.com"
  git config user.name >/dev/null 2>&1 || git config user.name "cpap-worker-$WID"
  BRANCH=$(git symbolic-ref --short HEAD) || { echo "worker requires a branch" >&2; return 1; }
  trap 'RUN_STATUS=interrupted; finish 130; exit $?' INT
  trap 'RUN_STATUS=terminated; finish 143; exit $?' TERM
  local plan now nh pid
  local alive=()
  while true; do
    if ! plan=$(read_plan) || ! parse_plan "$plan"; then
      RUN_STATUS=plan_failed; finish 1; return $?
    fi
    if [ "$P" = 0 ]; then RUN_STATUS=stopped; break; fi
    if ! resolve_code; then RUN_STATUS=code_unavailable; finish 1; return $?; fi
    if [ "$PLAN_TEXT" != "$ACTIVE_PLAN" ] || [ "$SOURCE_ID" != "$CURSOURCE" ]; then
      stop_procs
      if [ "$SOURCE_ID" != "$CURSOURCE" ] && ! update_code; then
        RUN_STATUS=build_failed; finish 1; return $?
      fi
      if ! start_procs; then RUN_STATUS=partition_failed; finish 1; return $?; fi
    fi
    alive=()
    for pid in "${PIDS[@]}"; do
      if kill -0 "$pid" 2>/dev/null; then alive+=("$pid");
      elif ! wait "$pid"; then RUN_STATUS=search_failed; finish 1; return $?; fi
    done
    PIDS=("${alive[@]}")
    if [ ${#PIDS[@]} = 0 ]; then RUN_STATUS=finished; break; fi
    now=$(date +%s); nh=$(hit_count)
    if [ "$nh" -gt "$LASTHITS" ]; then
      if ! verify_hits; then RUN_STATUS=verification_failed; finish 1; return $?; fi
      LASTHITS=$nh
      if ! publish; then stop_procs; return 1; fi
      LASTPUB=$now
    fi
    if [ "$((now - LASTPUB))" -ge 900 ]; then
      if ! publish; then stop_procs; return 1; fi
      LASTPUB=$now
    fi
    if [ "$((now - START))" -ge "$((MAXH * 3600))" ]; then RUN_STATUS=finished; break; fi
    sleep 120
  done
  finish 0
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then main "$@"; fi

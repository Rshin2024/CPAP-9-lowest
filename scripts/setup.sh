#!/usr/bin/env bash
# Install dependencies and build the search binaries.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ ! -f /usr/include/x86_64-linux-gnu/gmp.h ] && [ ! -f /usr/include/gmp.h ]; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq libgmp-dev pari-gp python3-gmpy2 tmux
fi
command -v gp >/dev/null 2>&1 || sudo apt-get install -y -qq pari-gp || true
python3 -c "import gmpy2" 2>/dev/null || sudo apt-get install -y -qq python3-gmpy2 || true
mkdir -p bin
gcc -O3 -march=native -Wall -o bin/search src/search.c -lgmp -lm
gcc -O2 -Wall -o bin/cover src/cover.c -lm
echo "build ok: $(nproc) cores"
# smoke test: reproduce the 2004 record CPAP-9
out=$(./bin/search -P 179 -X 149,157 -x 87103490338886343449123705322656962705040008760706629856986802283 \
  -k 3416716300000 -K 3416716400000)
if echo "$out" | grep -q "HIT CPAP-9 k=3416716311814"; then echo "smoke test ok"; else echo "SMOKE TEST FAILED"; echo "$out"; exit 1; fi

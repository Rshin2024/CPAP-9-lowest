#!/usr/bin/env bash
# Install dependencies, build, and reproduce the 48-digit result.
set -euo pipefail
cd "$(dirname "$0")/.."
PYTHON=${PYTHON:-python3}
if [ -x .venv/bin/python ] && [ "$PYTHON" = python3 ]; then PYTHON=.venv/bin/python; fi
case $(uname -s) in
  Darwin)
    command -v brew >/dev/null || { echo "Install Homebrew first." >&2; exit 1; }
    for package in gmp pari; do
      brew list --versions "$package" >/dev/null 2>&1 || brew install "$package"
    done
    ;;
  Linux)
    if ! command -v gp >/dev/null ||
       ! printf '#include <gmp.h>\n' | "${CC:-cc}" -E - >/dev/null 2>&1 ||
       ! "$PYTHON" -c 'import ensurepip, venv' >/dev/null 2>&1; then
      command -v apt-get >/dev/null || { echo "Install GMP headers, PARI/GP, and a C compiler." >&2; exit 1; }
      privilege=()
      if [ "$(id -u)" != 0 ]; then privilege=(sudo); fi
      "${privilege[@]}" apt-get update -qq
      "${privilege[@]}" apt-get install -y build-essential libgmp-dev pari-gp python3-venv python3-pip
    fi
    ;;
esac
command -v gp >/dev/null || { echo "PARI/GP (gp) is required." >&2; exit 1; }
if ! "$PYTHON" -c 'import gmpy2, sympy' >/dev/null 2>&1; then
  "$PYTHON" -m venv .venv
  PYTHON=.venv/bin/python
  "$PYTHON" -m pip install -r requirements.txt
fi
scripts/build.sh
out=$(./bin/search -P 103 -x 19506961754250869267574127632655931757917 -k 20963747 -K 20963750)
if ! [[ "$out" == *"HIT CPAP-9 k=20963748 "* ]]; then
  echo "$out" >&2
  echo "Search smoke test failed." >&2
  exit 1
fi
"$PYTHON" scripts/verify.py 502811815791820948505989265164219187224962352997 9
echo "Setup complete."

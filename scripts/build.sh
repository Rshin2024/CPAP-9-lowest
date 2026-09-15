#!/usr/bin/env bash
# Build both tools; install them only after both compilations succeed.
set -eo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SOURCE=${1:-$ROOT}
OUTPUT=${2:-$ROOT/bin}
mkdir -p "$OUTPUT"
BUILD=$(mktemp -d "$OUTPUT/.build.XXXXXX")
trap 'rm -rf -- "$BUILD"' EXIT

read -r -a cflags <<< "${CFLAGS:--O3 -Wall -Wextra}"
read -r -a cppflags <<< "${CPPFLAGS:-}"
read -r -a ldflags <<< "${LDFLAGS:-}"
GMP_ROOT=${GMP_PREFIX:-}
if [ -z "$GMP_ROOT" ] && command -v brew >/dev/null 2>&1; then
  GMP_ROOT=$(brew --prefix gmp 2>/dev/null || true)
fi
if [ -n "$GMP_ROOT" ]; then
  cppflags+=("-I$GMP_ROOT/include")
  ldflags+=("-L$GMP_ROOT/lib")
fi
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L "${cppflags[@]}" "${cflags[@]}" \
  "$SOURCE/src/search.c" "${ldflags[@]}" -lgmp -lm -o "$BUILD/search"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L "${cppflags[@]}" "${cflags[@]}" \
  "$SOURCE/src/cover.c" "${ldflags[@]}" -lm -o "$BUILD/cover"
mv "$BUILD/search" "$OUTPUT/search"
mv "$BUILD/cover" "$OUTPUT/cover"

#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-build-fast}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"
OPT_LEVEL="${OPT_LEVEL:-1}"

cd "$ROOT_DIR"
cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_CXX_FLAGS_RELEASE=-O${OPT_LEVEL} -DNDEBUG" \
  -DSES_ENABLE_UNITY_BUILD=ON \
  -DSES_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --target ses --parallel "$JOBS"

printf '\nBuilt app: %s/%s\n' "$BUILD_DIR" ses

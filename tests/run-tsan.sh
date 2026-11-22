#!/usr/bin/env bash
# ThreadSanitizer gate for the RT engine (DESIGN §13, ROADMAP P1 task 8).
# Uses a separate build dir; Qt-only suppressions live in tests/tsan.supp.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
cmake -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DSES_ENABLE_TSAN=ON -DSES_BUILD_TESTS=ON
cmake --build build-tsan --target tst_engine
TSAN_OPTIONS="suppressions=$root/tests/tsan.supp halt_on_error=1" ./build-tsan/tst_engine

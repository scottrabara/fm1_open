#!/usr/bin/env bash
# Builds the APP with the browser simulator's platform (web/engine) as a
# native program and runs the host tests. Needs clang (or CC=gcc).
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/host-test
mkdir -p "$out"
mapfile -t sources < <(find app web third_party/synth -name '*.c' -not -path 'app/board/*' | sort)
flags=()
if [[ "${SANITIZE:-}" == 1 ]]; then flags=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
for test in tools/host-test/*_test.c; do
  name=$(basename "$test" .c)
  "${CC:-clang}" -std=c11 -O1 -g -fwrapv -fno-fast-math -DFM1_HOST_TEST -DFM1_OPEN_WEB \
    -Wall -Wextra -Wno-unused-function -Wno-array-parameter \
    -Iweb/engine -Iweb/synth -Ithird_party/synth -Isdk/include -Iapp/synth -Iapp \
    ${flags[@]+"${flags[@]}"} "${sources[@]}" "$test" -lm -o "$out/$name"
  echo "== $name"
  "$out/$name"
done

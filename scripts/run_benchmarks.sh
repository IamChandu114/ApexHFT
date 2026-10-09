#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DAPEXHFT_ENABLE_TESTS=ON \
  -DAPEXHFT_ENABLE_BENCHMARKS=ON \
  -DAPEXHFT_ENABLE_SANITIZERS=OFF
cmake --build build-release --target apexhft_bench
./build-release/apexhft_bench

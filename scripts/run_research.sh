#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAPEXHFT_ENABLE_TESTS=ON
cmake --build build --target apexhft_research
./build/apexhft_research --csv data/synthetic_quotes.csv

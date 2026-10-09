#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "$(id -u)" -eq 0 ]]; then
    echo "Refusing to run as root." >&2
    exit 1
fi

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAPEXHFT_ENABLE_TESTS=ON \
  -DAPEXHFT_ENABLE_SANITIZERS=OFF
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAPEXHFT_ENABLE_TESTS=ON \
  -DAPEXHFT_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

git diff --check
echo "Verification completed successfully."

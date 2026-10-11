#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "$(id -u)" -eq 0 ]]; then
    echo "Refusing to run as root." >&2
    exit 1
fi

ops="${APEXHFT_DIFF_OPS:-10000}"
build_dir="${APEXHFT_DIFF_BUILD:-build}"

if [[ ! -f "${build_dir}/build.ninja" ]]; then
    cmake -S . -B "${build_dir}" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DAPEXHFT_ENABLE_TESTS=ON \
      -DAPEXHFT_ENABLE_SANITIZERS=OFF
fi

cmake --build "${build_dir}" --target apexhft_tests

echo "APEXHFT_DIFF_OPS=${ops} seed=0xD1FF3E71A1 build=${build_dir}"
APEXHFT_DIFF_OPS="${ops}" ctest --test-dir "${build_dir}" \
  -R 'MatchingEngineDifferentialTest.SubmitAndCancelMatchReferenceModel' \
  --output-on-failure --verbose

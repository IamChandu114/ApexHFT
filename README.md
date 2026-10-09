# ApexHFT

A research-oriented, deterministic C++20 framework for studying tick-to-trade
mechanics through **offline** simulation. It does not submit live orders,
connect to brokers or exchanges, or handle real-money accounts.

## What is implemented

- Integer price ticks and quantities; limit-order matching with price-time
  priority and execution at the resting price.
- Pre-trade risk, an order-management layer, positions, and an in-memory audit log.
- Deterministic market replay and OMS command replay.
- A dependency-free Release benchmark harness.
- An offline research loop over **synthetic** top-of-book quotes (threshold
  mean-reversion vs buy-and-hold, walk-forward parameter selection).

Market-order matching, live data, and profitability are non-goals.

## Requirements

- GCC or Clang with C++20 (verified: GCC 13.3.0)
- CMake 3.25+
- Ninja
- GoogleTest CMake package (`GTest CONFIG`)

On Ubuntu 24.04 these packages are sufficient (install once; do not build as root):

```bash
sudo apt-get install -y g++ cmake ninja-build libgtest-dev
```

`./scripts/verify.sh` configures Debug and ASan/UBSan trees itself. Never run
project commands as root.

## Debug build and tests

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAPEXHFT_ENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/apexhft
```

Verified in this tree: **92/92** tests passed.

## Sanitizer build

```bash
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DAPEXHFT_ENABLE_TESTS=ON \
  -DAPEXHFT_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Verified: **92/92** tests passed. Combined gate: `./scripts/verify.sh`
(also runs `git diff --check`).

## Replay

Market replay (`MarketReplay`) and OMS replay (`OmsReplay`) are libraries
exercised by unit tests:

```bash
ctest --test-dir build -R 'Replay' --output-on-failure
```

Identical sequenced inputs produce identical trades, audit sequences, and
positions. See `include/apexhft/replay.hpp` and `include/apexhft/oms_replay.hpp`.

## Benchmarks

Release only; do not quote sanitizer timings.

```bash
./scripts/run_benchmarks.sh
```

Machine-specific captured output:

- Baseline: `docs/results/phase7_release_baseline.txt`
- After OMS lookup change: `docs/results/phase8_release_optimized.txt`

Details: `docs/BENCHMARKING.md`.

## Offline research

```bash
cmake --build build --target apexhft_research
./build/apexhft_research --csv data/synthetic_quotes.csv
# or
./scripts/run_research.sh
```

`data/synthetic_quotes.csv` is labelled **synthetic**, not historical
market data. Schema:

```
timestamp_ns,bid_ticks,ask_ticks,bid_qty,ask_qty
```

Unsorted or invalid rows are rejected. Strategy: buy after a mid drop of
`entry_ticks`, sell after a mid rise while long; fees and slippage are
integer ticks. Walk-forward selects `entry_ticks` on the first half of the
file and evaluates the second half. Synthetic PnL is not a trading edge.

## Design notes

- Single-threaded, deterministic, no wall-clock in the engine.
- Audit logs are outputs; replay commands plus `RiskLimits` are the inputs.
- Exception safety: rejections before matching leave state unchanged; after
  a successful match, audit/position failures are not transactional.

See `docs/ARCHITECTURE.md` and `docs/FEATURE_STATUS.md`.

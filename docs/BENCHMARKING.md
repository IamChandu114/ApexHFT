# Benchmarking

Do not treat sanitizer or Debug timings as production-like results.

## Build and run

```bash
./scripts/run_benchmarks.sh
# equivalent:
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DAPEXHFT_ENABLE_TESTS=ON \
  -DAPEXHFT_ENABLE_BENCHMARKS=ON \
  -DAPEXHFT_ENABLE_SANITIZERS=OFF
cmake --build build-release --target apexhft_bench
./build-release/apexhft_bench
```

No extra benchmark framework. Timing uses `std::chrono::steady_clock`.

## Methodology

- Warm-up: 1 untimed repetition.
- Timed repetitions: 5. Report **min** and **median** elapsed milliseconds.
- `ops_per_sec` uses median elapsed and the timed operation count.
- Setup (pre-populating books) is excluded from elapsed time except where a
  workload is a single batch (`market_replay`, `oms_replay`).
- Per-operation p50/p95/p99 are taken from the **first timed repetition**
  only. They include clock overhead and are noisy below ~100 ns.
- Batch workloads do not report per-op percentiles.
- PRNG: LCG `state = state * 6364136223846793005 + 1`, seed **20261009**.

## Workloads

| Name | Timed ops | Notes |
| --- | --- | --- |
| `insert_empty` | 10,000 | Non-crossing inserts into an empty book |
| `insert_non_crossing_book1000` | 5,000 | After 1,000 setup orders |
| `insert_non_crossing_book10000` | 5,000 | After 10,000 setup orders |
| `marketable_trades` | 10,000 | Buys taking 10,000 resting sells |
| `cancel_existing` | 5,000 | Cancel previously inserted resting orders |
| `mixed_deterministic` | 20,000 | ~70% insert / 30% cancel of **active** IDs |
| `market_replay` | 20,000 | `MarketReplay::run` batch |
| `oms_replay` | 8,000 | 2,000 fill pairs + 2,000 rest+cancel |

## Environment (measured host)

Machine-specific. WSL2 Ubuntu 24.04.4 LTS, Linux 6.6.87.2-microsoft-standard-WSL2,
12th Gen Intel Core i5-1235U, gcc 13.3.0, C++20, Release, sanitizers off.

## Phase 7 baseline

Captured in `docs/results/phase7_release_baseline.txt` **before** the Phase 8
OMS change. Median `oms_replay`: **132.028 ms**, **60,593** ops/s.

Matching cancel is relatively expensive (linear removal in a price-level
deque) even in the baseline.

## Phase 8 comparison

Hypothesis: OMS command cost was dominated by scanning **all historical**
owned IDs for ownership, self-cross, and pending exposure.

Change kept: `unordered_set` for ownership and an `active_ids_` list for
live owned orders. No second matching engine, allocator, or threads.

Same Release binary flags and workloads after the change:
`docs/results/phase8_release_optimized.txt`.

| workload | baseline median ops/s | after change | notes |
| --- | --- | --- | --- |
| oms_replay | 60,593.0 | 1,546,270.5 | target metric; 132.028/5.174 ≈ 25.5× |
| insert_empty | 7,483,506 | 10,991,451 | matching engine; run noise |
| cancel_existing | 797,136 | 617,923 | not the changed path |

Matching-engine rows moved with WSL noise; they were not the optimization
target. The OMS row is the comparable evidence for keeping the change.

## Limitations

- Results are host-specific and not a latency SLA.
- Percentiles are not isolated hardware timestamps.
- `cancel_existing` still scans FIFO deques; it was not modified in Phase 8.

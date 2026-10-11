# ApexHFT- Deterministic C++20 Trading-System Simulator | Matching Engine, Risk Controls & Differential Testing
 

[![ApexHFT CI](https://github.com/IamChandu114/ApexHFT/actions/workflows/ci.yml/badge.svg)](https://github.com/IamChandu114/ApexHFT/actions/workflows/ci.yml)

A research-oriented, deterministic C++20 framework for studying tick-to-trade
mechanics through **offline** simulation. It does not submit live orders,
connect to brokers or exchanges, or handle real-money accounts.

---

## What ApexHFT Is and What It Is Not

- **What it is:** An offline, single-threaded, deterministic C++20 order-matching and simulation framework with price-time priority, integer arithmetic, pre-trade risk controls, order management, audit logging, deterministic event/OMS replay, differential fuzz testing against an independent reference book, and backtesting on synthetic quotes.
- **What it is not:** A live trading platform, broker gateway, exchange connectivity layer (no FIX, ITCH, or OUCH), multi-threaded matching engine, or profitable trading algorithm.

---

## What Is Implemented

- **Core Domain & Types:** Integer price ticks and quantities; limit-order lifecycle (`New`, `PartiallyFilled`, `Filled`, `Cancelled`, `Rejected`).
- **Reference Matching Engine:** Limit-order matching, price-time priority, resting-price execution, monotonic trade IDs, $O(L)$ level cancellation.
- **Pre-Trade Risk & OMS:** Order quantity and position limits, price bands, self-cross prevention, external liquidity, and in-memory monotonic audit log (`EventLog`).
- **Deterministic Replay:** Exact reconstruction of trades, positions, and audit streams via `MarketReplay` and `OmsReplay`.
- **Differential Validation:** Randomized submit/cancel testing against an independent reference order book (seed `0xD1FF3E71A1`), verified to 100,000 operations with detailed mismatch diagnostics.
- **Release Benchmarking:** Dependency-free microbenchmark suite measuring throughput and latency across 8 core workloads.
- **Offline Research Pipeline:** Walk-forward threshold mean-reversion strategy evaluated against buy-and-hold on **synthetic** top-of-book data.

Market orders, live market data, and universal speedup claims are explicit non-goals.

---

## Documentation and Evidence Links

- [Release Readiness Report](docs/RELEASE_READINESS.md) — Comprehensive audit, verified results, and scaling analysis
- [Release Verification Record](docs/results/release_verification.txt) — Raw machine-readable verification transcript
- [System Architecture](docs/ARCHITECTURE.md) — Design, invariants, event flow, and exception guarantees
- [Feature Status](docs/FEATURE_STATUS.md) — Verification matrix across all project phases
- [Benchmarking Methodology](docs/BENCHMARKING.md) — Workload specifications, scaling derivation, and raw numbers
- [Implementation Plan](docs/IMPLEMENTATION_PLAN.md) — Roadmap status and delivery confirmation

---

## Build Prerequisites

- GCC or Clang with C++20 support (verified on GCC 13.3.0)
- CMake 3.25+
- Ninja build system
- GoogleTest CMake package (`GTest CONFIG`)

On Ubuntu 24.04 (do not build as root):
```bash
sudo apt-get install -y g++ cmake ninja-build libgtest-dev
```

---

## Verification and Testing

### 1. Canonical Verification Gate
Runs Debug (97 tests), ASan/UBSan (97 tests), Release (97 tests), and repository hygiene checks:
```bash
./scripts/verify.sh
```

### 2. Debug Build and Tests
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAPEXHFT_ENABLE_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/apexhft
```
*Verified:* **97/97 passed** (0.81s).

### 3. AddressSanitizer and UndefinedBehaviorSanitizer
```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAPEXHFT_ENABLE_TESTS=ON -DAPEXHFT_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```
*Verified:* **97/97 passed** (3.68s, 0 sanitizer defects).

### 4. Release Build and Tests
```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DAPEXHFT_ENABLE_TESTS=ON -DAPEXHFT_ENABLE_BENCHMARKS=ON -DAPEXHFT_ENABLE_SANITIZERS=OFF
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```
*Verified:* **97/97 passed** (0.47s).

---

## Randomized Differential Testing

Validates matching engine execution against an independent reference book under randomized submit/cancel operations with fixed seed `0xD1FF3E71A1`:

```bash
# Standard 10,000 operation run
APEXHFT_DIFF_OPS=10000 ./scripts/run_differential.sh

# Extended 100,000 operation run (Release build)
APEXHFT_DIFF_BUILD=build-release APEXHFT_DIFF_OPS=100000 ./scripts/run_differential.sh
```
*Status:* **100,000 operations passed** (24.19s). (Note: The 1,000,000 operation test is documented as interrupted due to cumulative $O(N^2)$ price-queue cancellation scaling under concentrated depth; see [RELEASE_READINESS.md](docs/RELEASE_READINESS.md)).

---

## Benchmarking

Execute the dependency-free Release benchmark suite:
```bash
./scripts/run_benchmarks.sh
```
- Baseline metrics: `docs/results/phase7_release_baseline.txt`
- Optimized OMS metrics: `docs/results/phase8_release_optimized.txt` (25.5× speedup on `oms_replay`)

---

## Offline Research Pipeline

```bash
cmake --build build --target apexhft_research
./build/apexhft_research --csv data/synthetic_quotes.csv
# or
./scripts/run_research.sh
```
*Note:* `data/synthetic_quotes.csv` is labelled **synthetic**. Backtest metrics are deterministic proof of accounting mechanics, not predictive trading edge.

---

## Interview Demonstration Workflow

To demonstrate the full repository in under 2 minutes from a clean shell:
```bash
# 1. Canonical multi-target verification (Debug, ASan, Release)
./scripts/verify.sh

# 2. Run deterministic differential model comparison (10k ops)
APEXHFT_DIFF_OPS=10000 ./scripts/run_differential.sh

# 3. Run synthetic research backtest
./scripts/run_research.sh

# 4. Run Release benchmark suite
./scripts/run_benchmarks.sh
```

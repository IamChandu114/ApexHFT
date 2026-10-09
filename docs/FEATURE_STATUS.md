# Feature Status

Statuses: **Verified** | Implemented — verification pending | In progress | Planned | Blocked

## Phase 0 — Foundation
- **Verified.** C++20, CMake, Ninja, `-Wall -Wextra -Wpedantic -Werror`.
- GoogleTest/CTest. Debug and ASan/UBSan via `./scripts/verify.sh`.

## Phase 1 — Core Domain Model
- **Verified.** Integer ticks, quantities, IDs, limit/market construction,
  fills, cancel, reject (`tests/test_domain.cpp`).

## Phase 2 — Reference Matching Engine
- **Verified.** Limit orders, price-time priority, resting-price execution,
  partial fills, cancel, duplicate IDs, monotonic trade IDs
  (`tests/test_matching_engine.cpp`).
- Market-order matching is not implemented.

## Phase 3 — Engine differential tests
- **Verified.** Seeded stress plus independent reference model comparison.

## Phase 4 — Market replay
- **Verified.** Contiguous sequences, submit/cancel, deterministic trades
  (`tests/test_replay.cpp`).

## Phase 5 — Risk and OMS
- **Verified.** Pre-trade quantity/price/position checks, self-cross block,
  external liquidity, positions (`tests/test_risk.cpp`, `test_oms.cpp`,
  `test_oms_external.cpp`).

## Phase 6.1 / 6.2 — Audit log
- **Verified.** In-memory `EventLog` with contiguous sequences; OMS records
  accept/reject/cancel/trade (`tests/test_event_log.cpp`, `test_oms_audit.cpp`).

## Phase 6.3 — Deterministic OMS replay
- **Verified.** `OmsReplay` reconstructs position, trades, audit events, and
  remaining owned orders from sequenced commands. Identical streams match
  each other and direct OMS execution (`tests/test_oms_replay.cpp`).

## Phase 7 — Benchmarks
- **Verified.** `./scripts/run_benchmarks.sh` (Release, sanitizers off).
  Machine-specific output: `docs/results/phase7_release_baseline.txt`.

## Phase 8 — Evidence-based optimization
- **Verified.** OMS `is_own_order` was O(n) over all historical IDs and pending
  exposure scanned filled/cancelled orders. Hash-set ownership plus an active
  ID list improved `oms_replay` from **60,593.0** to **1,546,270.5** ops/s on the
  same host/workload. Correctness: 92/92 Debug and ASan tests.
  Comparison: `docs/results/phase8_release_optimized.txt`.

## Phase 9 — Offline research
- **Verified** as a simulator on synthetic top-of-book snapshots
  (`tests/test_research.cpp`, `./scripts/run_research.sh`).
- Dataset `data/synthetic_quotes.csv` is **synthetic**, not historical.

## Evidence (this environment)
- Debug: `ctest --test-dir build --output-on-failure` → **92/92 passed**.
- ASan/UBSan: `ctest --test-dir build-asan --output-on-failure` → **92/92 passed**.
- `./scripts/verify.sh` rebuilds both trees and runs `git diff --check`.
- Research repeated twice: identical SHA-256 of stdout.

## Out of scope
- Live exchange/broker connectivity and real-money execution.
- Queue-position realism, market impact, and historical edge claims.
- Multi-threaded matching.

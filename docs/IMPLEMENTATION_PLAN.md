# Implementation Plan

## Phase 0 — Foundation
- Establish C++20, CMake, Ninja, Git, strict warnings and unit tests.
- Verify Debug and AddressSanitizer/UndefinedBehaviorSanitizer builds.
- Record actual tool versions and test evidence.
- **Status:** Verified.

## Phase 1 — Domain Model
- Define strong types for prices, quantities, IDs, sides and order states.
- Specify events, execution reports, errors and invariants.
- Use integer price ticks and deterministic semantics.
- **Status:** Verified.

## Phase 2 — Reference Matching Engine
- Implement limit orders, price-time priority, partial fills and cancellation.
- Test rejections, ordering, lifecycle transitions and edge cases.
- **Status:** Verified. Market orders remain unsupported.

## Phase 3 — Optimized Matching Engine
- Optimize only after the reference implementation is correct.
- Differential-test optimized behavior against the reference model.
- **Status:** Verified. Hardened differential testing framework implemented
  (`tests/test_matching_engine_differential.cpp`, `./scripts/run_differential.sh`),
  validating limit-order matching, price-time priority, partial fills, resting orders,
  and cancellations against an independent reference book. Includes deterministic
  seeding, replayable traces, and failure diagnostics verified up to 100,000 operations.
  Phase 8 later applied an evidence-backed OMS lookup change, not a second matching engine.

## Phase 4 — Market Data and Replay
- Validate input records and replay events deterministically.
- Handle malformed and incomplete data explicitly.
- **Status:** Verified for sequenced matching-engine replay.

## Phase 5 — Risk, OMS and Accounting
- Add pre-trade risk checks, simulated order management, positions, fees and P&L.
- **Status:** Verified for OMS risk, self-cross prevention, positions, and
  audit logging. Strategy-level cash accounting landed in Phase 9.

## Phase 6 — End-to-End Pipeline
- Connect replay, strategy, risk, simulated execution and reporting.
- **6.1** In-memory event log — Verified.
- **6.2** OMS audit-log integration — Verified (`./scripts/verify.sh`, 61/61
  before 6.3).
- **6.3** Deterministic OMS replay — Verified (`OmsReplayTest.*`).
- Strategy dispatch over quotes is Phase 9, using the same OMS.

## Phase 7 — Benchmarking
- Document workloads, hardware, compiler, build mode and methodology.
- Report measured throughput and latency distributions, not invented numbers.
- **Status:** Verified. Dependency-free `apexhft_bench` plus
  `docs/results/phase7_release_baseline.txt`.

## Phase 8 — Profiling and Concurrency
- Profile before optimizing; add concurrency only with explicit correctness tests.
- **Status:** Verified without introducing concurrency. OMS owned-ID lookup
  and active-order scans were the measured bottleneck; see
  `docs/BENCHMARKING.md`. The engine remains single-threaded.

## Phase 9 — Strategy Research
- Evaluate a baseline strategy with fees, slippage, risk controls and leakage checks.
- **Status:** Verified on labelled **synthetic** quotes. Not a claim of
  live-market edge.

## Phase 10 — Delivery
- Provide reproducible commands, sample data, architecture diagrams and demo results.
- **Status:** Verified. Canonical verification (`verify.sh`) covers Debug, ASan/UBSan, and Release builds with CTest suite (97/97 tests), deterministic 100k differential testing, GitHub CI workflow with automated checks, and release readiness evidence.
- **Finalization Note:** All planned offline simulator features (Phases 0–10) are complete and verified. Deferred items (market order execution in engine, $O(1)$ intrusive list book cancellation, multi-threaded matching, exchange connectivity) remain explicitly out-of-scope non-goals.

Each phase requires review and passing relevant tests before the next begins.

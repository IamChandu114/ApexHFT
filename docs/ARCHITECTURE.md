# Architecture

ApexHFT is an offline, single-threaded C++20 simulator. There is no network
path to brokers or exchanges.

## Module responsibilities

| Module | Role |
| --- | --- |
| `domain.hpp` | Strong types: ticks, quantity, IDs, `Order` lifecycle |
| `matching_engine.hpp` | Limit book, price-time priority, trades |
| `replay.hpp` | Sequenced submit/cancel against a fresh matching engine |
| `risk.hpp` | Pre-trade quantity, price band, worst-case position |
| `event_log.hpp` | In-memory audit events with monotonic sequences |
| `oms.hpp` | Owned orders, self-cross, risk, positions, audit |
| `oms_replay.hpp` | Sequenced reconstruction of OMS state |
| `research.hpp` | Quote CSV, threshold strategy, backtest, metrics |

Public headers live under `include/apexhft/`. Logic is header-only except
`version.cpp` and CLI entry points.

## Data flow

```
Quote CSV or SequencedEvent / OmsReplayCommand
        -> validation
        -> MatchingEngine and/or OrderManagementSystem
        -> Trade list, EventLog, position
        -> research accounting / metrics
```

## Matching and priority

- Limit orders only. Incoming market orders throw in the engine and are
  rejected by the OMS without throwing.
- Best price first; FIFO at a price (`std::map` of `std::deque`).
- Trade price is the **resting** order's limit.
- Duplicate order IDs throw `std::invalid_argument`.
- Cancel of unknown IDs throws `std::out_of_range`; inactive cancel throws
  `std::logic_error`.

## OMS and risk

- Self-cross: an owned live order that would trade with a new owned order is
  rejected (`OrderRejected` audit, book unchanged).
- Risk uses current position plus pending owned exposure on each side.
- `add_external_liquidity` is not risk-checked and is not cancellable via OMS
  `cancel` (non-owned).
- Position updates only when an owned order is a trade party.

## Audit vs replay input

Audit events are **derived output**. They are not sufficient to rebuild the
OMS: missing external-liquidity commands, initial `RiskLimits`, and rejected
command payloads that never entered the book.

`OmsReplayCommand` is the input model (`SubmitOwned`, `CancelOwned`,
`AddExternalLiquidity`) with contiguous sequences starting at 1.
`OmsReplayResult` exposes trades, audit copy, per-command accept/reject,
position, and remaining owned orders.

Invalid sequences throw before returning a result. Risk/self-cross/type
rejections are recorded and replay continues. Duplicate IDs and illegal
cancels throw, matching direct OMS.

## Exception behavior

- Rejections before `MatchingEngine::submit` leave book and position unchanged.
- After a successful engine submit, a later audit-log allocation or position
  overflow is **not** rolled back (basic exception safety, not transactional).
  Active-order tracking is updated from engine state before audit appends.
- Event sequence exhaustion sets the next sequence to 0 and later appends throw.

## Strategy research pipeline

1. Load `timestamp_ns,bid_ticks,ask_ticks,bid_qty,ask_qty`.
2. Unsorted timestamps, missing fields, non-positive prices/qty, or locked
   books (`bid >= ask`) are rejected; data is never silently repaired.
3. Each snapshot is independent top-of-book liquidity for that timestamp.
4. Threshold mean-reversion uses only the previous mid and current quote.
5. Execution: add external liquidity at displayed bid/ask ± slippage, submit
   an owned limit, cancel any residual (IOC-like). OMS risk still applies.
6. Accounting is integer tick-lots: cash ± price×qty − fee×qty; equity =
   cash + position × current mid.

## Known limitations

- No market-order matching, no multi-venue routing, no wall-clock time.
- Quote snapshots cannot reconstruct queue priority or impact.
- External residual liquidity can remain after an owned reject on that bar.
- Hash iteration is not used for book order; price-time uses ordered maps.
- Order cancellation scans FIFO deques linearly (`std::remove` in price-level `std::deque`), which is $O(L)$ where $L$ is the number of resting orders at that price. Under very large backlogs with hundreds of thousands of resting orders, cancellation overhead scales quadratically over long runs.

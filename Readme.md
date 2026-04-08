# Limit Order Book Engine

[![CI](https://github.com/drishyashrestha/Limit-Order-Book/actions/workflows/ci.yml/badge.svg)](https://github.com/drishyashrestha/Limit-Order-Book/actions)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

A **price-time priority matching engine** built in modern C++17 — no external libraries, no frameworks, just the STL and deliberate data structure choices.

This mirrors the core of what electronic exchanges (NASDAQ, CME, Eurex) run at the heart of their trading infrastructure: an order book that continuously matches incoming buy and sell orders against resting liquidity, at millions of operations per second.

---

## Performance

Benchmarked on 100k and 1M randomised limit orders (prices ∈ {99, 100, 101}, quantities ∈ [1, 10], ~50/50 buy/sell split). All I/O suppressed during the timed window — pure matching throughput.

| Orders    | Trades Executed | Time       | Throughput             |
|-----------|-----------------|------------|------------------------|
| 100,000   | 80,323          | 17.82 ms   | **~5.6M orders/sec**   |
| 1,000,000 | 805,743         | 161.63 ms  | **~6.2M orders/sec**   |

> Hardware: Windows 10 / MSYS2 / g++ 13.2.0 / `-O2`

---

## Architecture

```
incoming order
      │
      ▼
 addOrder()
      │
      ├─ isBuy?  ──► matchBuy()   walks ASK side: lowest price first
      │
      └─ isSell? ──► matchSell()  walks BID side: highest price first
                          │
                  fills against resting orders (FIFO per level)
                          │
                          ▼
                   trades[] — append-only trade log


          ┌──────────────────────────────────────┐
          │            ORDER BOOK                │
          │                                      │
          │  ASKS  map<price, queue<Order>>       │
          │  ─────────────────────────────────   │
          │  103  │ [sell #6  qty=5]             │
          │  102  │ [sell #4  qty=8]             │
          │  101  │ [sell #5  qty=3]  ← best ask │
          │  ─────────── SPREAD ────────────── ─ │
          │  100  │ [buy  #1  qty=10] ← best bid │
          │   99  │ [buy  #2  qty=5]             │
          │   98  │ [buy  #3  qty=8]             │
          │  BIDS  map<price, queue<Order>>       │
          └──────────────────────────────────────┘
```

---

## Data Structures & Trade-offs

| Component | Structure | Rationale |
|-----------|-----------|-----------|
| Bid / Ask sides | `std::map<int, std::queue<Order>>` | Sorted automatically — O(log n) insert, O(1) best bid/ask via `rbegin()` / `begin()` |
| Per-price level | `std::queue<Order>` | Enforces FIFO time priority within the same price; O(1) front-of-queue dispatch |
| Order lookup | `std::unordered_map<int, Order>` | O(1) average cancel by ID; also acts as the **live-order authority** for lazy removal |
| Trade log | `std::vector<Trade>` | Append-only; cache-friendly sequential writes |

### Why `std::map` for the price ladder?

Matching requires the **best bid** (max key) and **best ask** (min key) on every incoming order. `std::map` keeps keys sorted in O(log n) and exposes `rbegin()` / `begin()` for O(1) extremum access. An `unordered_map` would be faster for point lookups but has no ordering — you'd need a parallel sorted structure, doubling memory and pointer-chasing overhead.

### Lazy cancel

When an order is cancelled, it is removed from `orderLookup` in O(1). The stale entry in the price-level queue is left in place and detected on the matching engine's next pass through that level — at which point it is popped in O(1) and skipped. This avoids an O(n) queue traversal on every cancel and is standard practice in real matching engines.

`orderLookup` is the **single source of truth** for whether an order is live. The queues are treated as fast-path access structures, not authoritative records.

---

## Complexity

| Operation | Time Complexity |
|-----------|----------------|
| Add limit order (no match) | O(log n) |
| Add limit order (match, k levels crossed) | O(k log n) |
| Add market order (sweeps k levels) | O(k log n) |
| Cancel order | O(1) lookup + O(1) mark |
| Best bid / best ask | O(1) |
| Book stats (volume scan) | O(L) — L = price levels |

*n = distinct price levels in the book*

---

## Key Design Decisions

### 1. Integer prices

Prices are stored as plain `int` (e.g., `10050` = $100.50 with 2 implied decimals). Floating-point price comparison is unreliable due to rounding, and integer arithmetic is faster. Real market data protocols — NASDAQ ITCH 5.0, CME MDP 3.0, FIX — all use integer prices with an implied scale.

### 2. I/O removed from the hot path

The first version printed every executed trade to `stdout`. With 80k trades per 100k orders, this caused 28 seconds of terminal I/O — making the engine appear **1,770× slower** than it actually was. A `silent` flag now suppresses all output during benchmarks, revealing true matching throughput.

In production, trade events would be written to a **lock-free SPSC ring buffer** consumed by a separate logging thread — the matching engine never blocks on I/O.

### 3. Market orders never rest

A market order instructs the engine to fill immediately at **any** available price. If the book has insufficient liquidity, the unfilled quantity is silently discarded (not queued at the touch). This matches the behaviour of real exchanges, where market orders are not eligible to rest.

### 4. Queue-per-level vs. one global sorted queue

Each price level maintains its own `std::queue<Order>` rather than one giant sorted queue. Benefits:
- O(1) FIFO dispatch within a level (just `front()` + `pop()`)
- No re-sorting when new orders arrive at existing prices
- Clean level eviction when a queue empties (`asks.erase(it)`)
- Memory is proportional to active orders, not the full price range

---

## Order Types Supported

| Type | Behaviour |
|------|-----------|
| **Limit** | Rests in the book at the specified price if not immediately matchable |
| **Market** | Fills immediately at the best available price(s); never rests |
| **Partial fill** | Any order may partially fill across multiple price levels |
| **Cancel** | Removes a resting order by ID via lazy tombstone removal |

---

## Build

### Prerequisites

- `g++` with C++17 support
- GNU `make`

**Windows:** use [MSYS2](https://www.msys2.org/)
```bash
pacman -S mingw-w64-x86_64-gcc make
```

### Commands

```bash
make          # build demo binary → ./lob
make test     # build + run 9 unit tests
make bench    # build + run benchmark (100k + 1M orders)
make clean    # remove all build artifacts
```

### Sample output

```
=== LIMIT ORDER BOOK ENGINE ===

Adding resting orders...

=== ORDER BOOK ===
  ASKS (lowest first):
    103  x  5
    102  x  8
    101  x  3
  --------
  BIDS (highest first):
    100  x  10
     99  x  5
     98  x  8
==================

  [BOOK STATS]
  Best Bid:        100
  Best Ask:        101
  Spread:          1
  Mid Price:       100.5
  Total Bid Vol:   23
  Total Ask Vol:   16
```

---

## Project Structure

```
.
├── include/
│   └── order_book.hpp          # Structs (Order, Trade, BookStats) + OrderBook interface
├── src/
│   ├── order_book.cpp          # Matching engine implementation
│   └── main.cpp                # Demo: resting, limit cross, market sweep, cancel
├── tests/
│   └── test_order_book.cpp     # 9 unit tests — no external framework
├── benchmarks/
│   └── bench.cpp               # Throughput benchmark (100k + 1M orders)
├── .github/
│   └── workflows/
│       └── ci.yml              # GitHub Actions: build + test on every push
├── Makefile
├── .gitignore
└── README.md
```

---

## What I'd Add Next

| Feature | Why It Matters |
|---------|----------------|
| **Array-based price ladder** | Replace `std::map` with a flat `std::array<Level, MAX_PRICE>` — O(1) best bid/ask with no tree traversal; standard in production LOBs where the price range is bounded |
| **Lock-free SPSC ring buffer** | Decouple trade logging to a dedicated I/O thread; remove the `silent` flag entirely; eliminate all I/O latency from the hot path |
| **NASDAQ ITCH 5.0 parser** | Reconstruct a live order book from real exchange feed data; validate the engine against production message flows |
| **GTC / IOC / FOK order types** | Good-Till-Cancel, Immediate-Or-Cancel, Fill-Or-Kill — essential for realistic algo strategies |
| **Order amend (modify)** | Change price or quantity of a resting order; critical for market-making without losing queue position |
| **Latency histogram** | Per-order nanosecond timestamps; p50/p99/p999 latency distribution instead of average throughput |

---

## License

MIT
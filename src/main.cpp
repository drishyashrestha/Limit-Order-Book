// ─────────────────────────────────────────────
//  main.cpp
//  Walkthrough of all core order book scenarios:
//    1. Resting limit orders — spreads & depth
//    2. Limit order crossing the spread → trade
//    3. Market order with multi-level slippage
//    4. Order cancellation (lazy removal)
//    5. Benchmark: 1,000,000 randomised orders
// ─────────────────────────────────────────────

#include "../include/order_book.hpp"
#include <iostream>

int main() {
    OrderBook book;
    std::cout << "=== LIMIT ORDER BOOK ENGINE ===\n\n";

    // ── 1. Resting orders ─────────────────────────────────────────────────
    // Place 3 bids (100, 99, 98) and 3 asks (101, 102, 103).
    // No prices cross → no trades, full depth rests in the book.
    std::cout << "Adding resting orders...\n";
    book.addOrder({1, 100, 10, true,  false});  // buy  10 @ 100
    book.addOrder({2,  99,  5, true,  false});  // buy   5 @ 99
    book.addOrder({3,  98,  8, true,  false});  // buy   8 @ 98
    book.addOrder({4, 102,  8, false, false});  // sell  8 @ 102
    book.addOrder({5, 101,  3, false, false});  // sell  3 @ 101
    book.addOrder({6, 103,  5, false, false});  // sell  5 @ 103
    book.printBook();

    // ── 2. Limit sell that crosses the spread ────────────────────────────
    // Sell 6 @ 100 — best bid is 100 (order #1, qty=10) → immediately trades.
    // Order #1 partially fills: 10 - 6 = 4 remaining @ 100.
    std::cout << "--- Limit sell @ 100 (crosses spread) ---\n";
    book.addOrder({7, 100, 6, false, false});
    book.printBook();

    // ── 3. Market buy — fills then walks up the ask ladder ───────────────
    // Buy 4 @ market.  Level 101 has only 3 (order #5) → filled, level gone.
    // Needs 1 more → sweeps to level 102 (order #4, qty=8) → fills 1.
    // This price deterioration is called "slippage".
    std::cout << "--- Market buy: qty=4, fills @ 101 then 102 (slippage) ---\n";
    book.addOrder({8, 0, 4, true, true});
    book.printBook();

    // ── 4. Cancel order #2 (buy 5 @ 99) ─────────────────────────────────
    // Removed from orderLookup immediately (O(1)).
    // The stale queue entry is pruned lazily on next match at price 99.
    std::cout << "--- Cancel order #2 ---\n";
    book.cancelOrder(2);
    book.printBook();

    // ── 5. Benchmark: 1,000,000 randomised limit orders ──────────────────
    book.benchmark(1'000'000);

    return 0;
}

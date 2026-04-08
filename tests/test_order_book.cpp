// ─────────────────────────────────────────────
//  test_order_book.cpp
//  Unit tests for the limit order book engine.
//  No external framework — pure C++17 + STL.
//
//  Build:
//    g++ -std=c++17 -O2 -I include \
//        tests/test_order_book.cpp src/order_book.cpp \
//        -o test_runner && ./test_runner
//
//  Or via Makefile:  make test
// ─────────────────────────────────────────────

#include "../include/order_book.hpp"
#include <iostream>
#include <string>

// ─── Minimal test harness ─────────────────────

static int s_passed = 0;
static int s_failed = 0;
static int s_prev_failed = 0;

#define ASSERT_EQ(a, b)                                                         \
    do {                                                                        \
        auto _a = (a); auto _b = (b);                                          \
        if (_a != _b) {                                                         \
            std::cerr << "    FAIL  " << #a << " == " << #b                    \
                      << "  (got " << _a << ", expected " << _b << ")\n";      \
            ++s_failed; return;                                                 \
        }                                                                       \
    } while (0)

#define ASSERT_TRUE(expr)                                                       \
    do {                                                                        \
        if (!(expr)) {                                                          \
            std::cerr << "    FAIL  expected true: " << #expr << "\n";         \
            ++s_failed; return;                                                 \
        }                                                                       \
    } while (0)

#define RUN(test)                                                               \
    do {                                                                        \
        test();                                                                 \
        if (s_failed == s_prev_failed) {                                        \
            std::cout << "  PASS  " << #test << "\n";                          \
            ++s_passed;                                                         \
        }                                                                       \
        s_prev_failed = s_failed;                                               \
    } while (0)

// ─────────────────────────────────────────────
//  TEST CASES
// ─────────────────────────────────────────────

// 1. Limit orders with no price overlap rest without trading
void test_limit_orders_rest_without_trading() {
    OrderBook book;
    book.addOrder({1, 100, 10, true,  false});  // buy  @ 100
    book.addOrder({2, 102,  5, false, false});  // sell @ 102 — spread = 2, no match

    ASSERT_EQ(book.tradeCount(), 0);

    BookStats s = book.getStats();
    ASSERT_EQ(s.bestBid,        100);
    ASSERT_EQ(s.bestAsk,        102);
    ASSERT_EQ(s.spread,           2);
    ASSERT_EQ(s.totalBidVolume,  10);
    ASSERT_EQ(s.totalAskVolume,   5);
}

// 2. Limit order crossing the spread triggers an immediate trade
void test_limit_order_crosses_spread() {
    OrderBook book;
    book.addOrder({1, 100, 10, true,  false});   // resting buy  @ 100
    book.addOrder({2, 100,  4, false, false});   // sell @ 100 → crosses bid

    ASSERT_EQ(book.tradeCount(), 1);

    const Trade& t = book.getTrades()[0];
    ASSERT_EQ(t.buyOrderId,   1);
    ASSERT_EQ(t.sellOrderId,  2);
    ASSERT_EQ(t.price,      100);
    ASSERT_EQ(t.quantity,     4);

    // 6 shares remain resting on the bid
    BookStats s = book.getStats();
    ASSERT_EQ(s.totalBidVolume, 6);
    ASSERT_EQ(s.totalAskVolume, 0);
}

// 3. Market buy fills at best ask, then walks to next level (slippage)
void test_market_buy_sweeps_levels() {
    OrderBook book;
    book.addOrder({1, 101, 3, false, false});   // sell 3 @ 101
    book.addOrder({2, 102, 5, false, false});   // sell 5 @ 102

    // Market buy qty=5: take all 3 @ 101, then 2 more @ 102
    book.addOrder({3, 0, 5, true, true});

    ASSERT_EQ(book.tradeCount(), 2);

    const auto& trades = book.getTrades();
    ASSERT_EQ(trades[0].price,    101);
    ASSERT_EQ(trades[0].quantity,   3);
    ASSERT_EQ(trades[1].price,    102);
    ASSERT_EQ(trades[1].quantity,   2);

    // 3 shares remain @ 102
    BookStats s = book.getStats();
    ASSERT_EQ(s.bestAsk,           102);
    ASSERT_EQ(s.totalAskVolume,      3);
}

// 4. Market sell fills at best bid
void test_market_sell_fills_best_bid() {
    OrderBook book;
    book.addOrder({1, 100, 8, true,  false});   // buy 8 @ 100
    book.addOrder({2,   0, 5, false,  true});   // market sell 5

    ASSERT_EQ(book.tradeCount(), 1);
    ASSERT_EQ(book.getTrades()[0].price,    100);
    ASSERT_EQ(book.getTrades()[0].quantity,   5);

    // 3 remain on the bid
    ASSERT_EQ(book.getStats().totalBidVolume, 3);
}

// 5. Partial fill: order stays in book with reduced quantity
void test_partial_fill_leaves_remainder() {
    OrderBook book;
    book.addOrder({1, 100, 10, true,  false});   // buy  10 @ 100
    book.addOrder({2, 100,  3, false, false});   // sell  3 @ 100 → partial fill

    ASSERT_EQ(book.tradeCount(), 1);
    ASSERT_EQ(book.getTrades()[0].quantity, 3);
    ASSERT_EQ(book.getStats().totalBidVolume, 7);   // 10 - 3 = 7 remain
    ASSERT_EQ(book.getStats().totalAskVolume, 0);
}

// 6. Cancel removes an order from active inventory
void test_cancel_removes_order() {
    OrderBook book;
    book.addOrder({1, 100, 10, true, false});
    book.addOrder({2,  99,  5, true, false});

    ASSERT_EQ(book.getStats().totalBidVolume, 15);

    book.cancelOrder(2);

    // Cancelled order excluded from stats via lookup check
    ASSERT_EQ(book.getStats().totalBidVolume, 10);
    ASSERT_EQ(book.tradeCount(), 0);
}

// 7. Cancelled order is not filled by a subsequent incoming order
void test_cancelled_order_not_matched() {
    OrderBook book;
    book.addOrder({1, 100, 5, true, false});   // resting buy
    book.cancelOrder(1);                        // cancel it

    // Sell @ 100 should find nothing to match
    book.addOrder({2, 100, 5, false, false});
    ASSERT_EQ(book.tradeCount(), 0);

    // The sell should now sit as the best ask
    ASSERT_EQ(book.getStats().totalAskVolume, 5);
}

// 8. Cancelling a non-existent order does not crash
void test_cancel_nonexistent_is_safe() {
    OrderBook book;
    book.cancelOrder(999);                // must not throw or crash
    ASSERT_EQ(book.tradeCount(), 0);
}

// 9. FIFO time priority within the same price level
void test_fifo_time_priority() {
    OrderBook book;
    book.addOrder({1, 100, 5, true, false});   // buy 5 @ 100 — first in queue
    book.addOrder({2, 100, 5, true, false});   // buy 5 @ 100 — second in queue

    // Sell 5 @ 100 — should match order #1 (FIFO), not #2
    book.addOrder({3, 100, 5, false, false});

    ASSERT_EQ(book.tradeCount(), 1);
    ASSERT_EQ(book.getTrades()[0].buyOrderId, 1);   // #1 filled first

    // Order #2 still rests
    ASSERT_EQ(book.getStats().totalBidVolume, 5);
}

// ─────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────
int main() {
    std::cout << "\n=== ORDER BOOK UNIT TESTS ===\n\n";

    RUN(test_limit_orders_rest_without_trading);
    RUN(test_limit_order_crosses_spread);
    RUN(test_market_buy_sweeps_levels);
    RUN(test_market_sell_fills_best_bid);
    RUN(test_partial_fill_leaves_remainder);
    RUN(test_cancel_removes_order);
    RUN(test_cancelled_order_not_matched);
    RUN(test_cancel_nonexistent_is_safe);
    RUN(test_fifo_time_priority);

    std::cout << "\n─────────────────────────────────\n";
    if (s_failed == 0)
        std::cout << "  All " << s_passed << " tests passed.\n";
    else
        std::cout << "  " << s_passed << " passed / " << s_failed << " FAILED\n";
    std::cout << "─────────────────────────────────\n\n";

    return s_failed > 0 ? 1 : 0;
}

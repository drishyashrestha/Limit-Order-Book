#pragma once

// ─────────────────────────────────────────────
//  order_book.hpp
//  Public interface for the limit order book engine.
//
//  Price-time priority matching:
//    → Best price fills first
//    → Within same price, earliest order fills first (FIFO)
//
//  Supports: limit orders, market orders, cancellations.
//  Compile: g++ -std=c++17 -O2 -I include
// ─────────────────────────────────────────────

#include <map>
#include <queue>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────
//  DATA TYPES
// ─────────────────────────────────────────────

struct Order {
    int  id;
    int  price;      // integer price (e.g. 10050 = $100.50). 0 for market orders.
    int  quantity;
    bool isBuy;
    bool isMarket;   // true → fill immediately at any price, never rest in book
};

struct Trade {
    int buyOrderId;
    int sellOrderId;
    int price;
    int quantity;
};

struct BookStats {
    int    bestBid;
    int    bestAsk;
    int    spread;
    double midPrice;
    int    totalBidVolume;
    int    totalAskVolume;
};

// ─────────────────────────────────────────────
//  ORDER BOOK
//
//  Internal layout:
//    bids: map<price, queue<Order>>   sorted ascending → best bid = rbegin()
//    asks: map<price, queue<Order>>   sorted ascending → best ask = begin()
//    orderLookup: unordered_map<id, Order>  — O(1) cancel + live-order authority
// ─────────────────────────────────────────────

class OrderBook {
public:
    // ── Core operations ───────────────────────
    void addOrder(Order order);       // submit a limit or market order
    void cancelOrder(int orderId);    // lazy-cancel: mark dead, skip on next match

    // ── Inspection ────────────────────────────
    BookStats                  getStats()   const;
    const std::vector<Trade>&  getTrades()  const { return trades; }
    int                        tradeCount() const { return static_cast<int>(trades.size()); }

    // ── Display ───────────────────────────────
    void printBook()  const;   // depth-of-book ladder (asks above, bids below)
    void printStats() const;   // best bid/ask, spread, mid, total volumes

    // ── Benchmarking ──────────────────────────
    void benchmark(int numOrders = 100'000);  // pre-generates orders, times match loop only

    // ── Utility ───────────────────────────────
    void reset();   // clear all state (used between benchmarks and unit tests)

private:
    std::map<int, std::queue<Order>> bids;
    std::map<int, std::queue<Order>> asks;
    std::unordered_map<int, Order>   orderLookup;  // ground truth for live orders
    std::vector<Trade>               trades;
    bool                             silent = false;  // suppress cout in hot path

    void matchBuy (Order& buyOrder);
    void matchSell(Order& sellOrder);
};

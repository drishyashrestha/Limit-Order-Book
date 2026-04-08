// ─────────────────────────────────────────────
//  order_book.cpp
//  Matching engine implementation.
//
//  Matching algorithm:
//    Buy  order → walks asks from lowest price upward  (price-time priority)
//    Sell order → walks bids from highest price downward (price-time priority)
//
//  Cancel uses lazy removal:
//    The order is removed from orderLookup (the live-order authority).
//    The stale entry in the price-level queue is detected and skipped
//    the next time the matching engine visits that level.
// ─────────────────────────────────────────────

#include "../include/order_book.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

// ─────────────────────────────────────────────
//  matchBuy
//  Walk the ask side from the lowest available price upward.
//  Fill against resting sell orders until:
//    (a) the incoming buy order is fully filled, OR
//    (b) no ask exists at or below the buy limit price
//
//  Market orders bypass the price check (isMarket == true).
// ─────────────────────────────────────────────
void OrderBook::matchBuy(Order& buyOrder) {
    while (!asks.empty() && buyOrder.quantity > 0) {
        auto it     = asks.begin();        // cheapest ask
        int bestAsk = it->first;

        // Limit price check — market orders always pass
        if (!buyOrder.isMarket && buyOrder.price < bestAsk) break;

        std::queue<Order>& level = it->second;

        while (!level.empty() && buyOrder.quantity > 0) {
            Order& resting = level.front();

            // Lazy-cancel check: if this order ID is no longer in
            // orderLookup it was cancelled — discard and move on.
            if (resting.quantity == 0 ||
                orderLookup.find(resting.id) == orderLookup.end()) {
                level.pop();
                continue;
            }

            int filled = std::min(buyOrder.quantity, resting.quantity);

            trades.push_back({ buyOrder.id, resting.id, bestAsk, filled });

            if (!silent)
                std::cout << "  TRADE | buy #"  << buyOrder.id
                          << " x sell #"         << resting.id
                          << " | px: "           << bestAsk
                          << " | qty: "          << filled << "\n";

            buyOrder.quantity -= filled;
            resting.quantity  -= filled;

            if (resting.quantity == 0) {
                orderLookup.erase(resting.id);
                level.pop();
            }
        }
        if (level.empty()) asks.erase(it);
    }
}

// ─────────────────────────────────────────────
//  matchSell
//  Walk the bid side from the highest available price downward.
//  Fill against resting buy orders until:
//    (a) the incoming sell order is fully filled, OR
//    (b) no bid exists at or above the sell limit price
// ─────────────────────────────────────────────
void OrderBook::matchSell(Order& sellOrder) {
    while (!bids.empty() && sellOrder.quantity > 0) {
        auto it     = bids.end(); --it;   // highest bid
        int bestBid = it->first;

        if (!sellOrder.isMarket && sellOrder.price > bestBid) break;

        std::queue<Order>& level = it->second;

        while (!level.empty() && sellOrder.quantity > 0) {
            Order& resting = level.front();

            if (resting.quantity == 0 ||
                orderLookup.find(resting.id) == orderLookup.end()) {
                level.pop();
                continue;
            }

            int filled = std::min(sellOrder.quantity, resting.quantity);

            trades.push_back({ resting.id, sellOrder.id, bestBid, filled });

            if (!silent)
                std::cout << "  TRADE | buy #"  << resting.id
                          << " x sell #"         << sellOrder.id
                          << " | px: "           << bestBid
                          << " | qty: "          << filled << "\n";

            sellOrder.quantity -= filled;
            resting.quantity   -= filled;

            if (resting.quantity == 0) {
                orderLookup.erase(resting.id);
                level.pop();
            }
        }
        if (level.empty()) bids.erase(it);
    }
}

// ─────────────────────────────────────────────
//  addOrder
//  Route to the correct match function, then queue
//  any unfilled remainder as a resting limit order.
//  Market orders: unfilled remainder is discarded.
// ─────────────────────────────────────────────
void OrderBook::addOrder(Order order) {
    if (order.isBuy) matchBuy(order);
    else             matchSell(order);

    // Rest unfilled limit quantity in the book
    if (order.quantity > 0 && !order.isMarket) {
        if (order.isBuy) bids[order.price].push(order);
        else             asks[order.price].push(order);
        orderLookup[order.id] = order;
    }
}

// ─────────────────────────────────────────────
//  cancelOrder
//  O(1) removal from the live-order map.
//  The stale queue entry is cleaned up lazily
//  by the matching engine on its next pass.
// ─────────────────────────────────────────────
void OrderBook::cancelOrder(int orderId) {
    auto it = orderLookup.find(orderId);
    if (it == orderLookup.end()) {
        if (!silent)
            std::cout << "  CANCEL FAILED: order #" << orderId << " not found\n";
        return;
    }
    orderLookup.erase(it);
    if (!silent)
        std::cout << "  CANCELLED order #" << orderId << "\n";
}

// ─────────────────────────────────────────────
//  getStats
// ─────────────────────────────────────────────
BookStats OrderBook::getStats() const {
    BookStats s = {0, 0, 0, 0.0, 0, 0};

    if (!bids.empty()) {
        s.bestBid = bids.rbegin()->first;
        for (const auto& [price, q] : bids) {
            std::queue<Order> tmp = q;
            while (!tmp.empty()) {
                // Only count orders still live in the lookup
                if (orderLookup.count(tmp.front().id))
                    s.totalBidVolume += tmp.front().quantity;
                tmp.pop();
            }
        }
    }
    if (!asks.empty()) {
        s.bestAsk = asks.begin()->first;
        for (const auto& [price, q] : asks) {
            std::queue<Order> tmp = q;
            while (!tmp.empty()) {
                if (orderLookup.count(tmp.front().id))
                    s.totalAskVolume += tmp.front().quantity;
                tmp.pop();
            }
        }
    }
    if (s.bestBid > 0 && s.bestAsk > 0) {
        s.spread   = s.bestAsk - s.bestBid;
        s.midPrice = (s.bestBid + s.bestAsk) / 2.0;
    }
    return s;
}

void OrderBook::printStats() const {
    BookStats s = getStats();
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "\n  [BOOK STATS]\n";
    std::cout << "  Best Bid:        " << s.bestBid        << "\n";
    std::cout << "  Best Ask:        " << s.bestAsk        << "\n";
    std::cout << "  Spread:          " << s.spread         << "\n";
    std::cout << "  Mid Price:       " << s.midPrice       << "\n";
    std::cout << "  Total Bid Vol:   " << s.totalBidVolume << "\n";
    std::cout << "  Total Ask Vol:   " << s.totalAskVolume << "\n\n";
}

// ─────────────────────────────────────────────
//  printBook  —  depth-of-book ladder
// ─────────────────────────────────────────────
void OrderBook::printBook() const {
    std::cout << "\n=== ORDER BOOK ===\n";
    std::cout << "  ASKS (lowest first):\n";
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::queue<Order> tmp = it->second;
        int total = 0;
        while (!tmp.empty()) {
            if (orderLookup.count(tmp.front().id))
                total += tmp.front().quantity;
            tmp.pop();
        }
        if (total > 0)
            std::cout << "    " << it->first << "  x  " << total << "\n";
    }
    std::cout << "  --------\n";
    std::cout << "  BIDS (highest first):\n";
    for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
        std::queue<Order> tmp = it->second;
        int total = 0;
        while (!tmp.empty()) {
            if (orderLookup.count(tmp.front().id))
                total += tmp.front().quantity;
            tmp.pop();
        }
        if (total > 0)
            std::cout << "    " << it->first << "  x  " << total << "\n";
    }
    std::cout << "==================\n";
    printStats();
}

// ─────────────────────────────────────────────
//  benchmark
//  Pre-generates all orders outside the timed window.
//  silent=true suppresses all cout — pure matching throughput.
//  Calls reset() first so prior state doesn't skew results.
// ─────────────────────────────────────────────
void OrderBook::benchmark(int numOrders) {
    std::cout << "\n=== BENCHMARK: " << numOrders << " orders ===\n";
    silent = true;
    reset();

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> priceDist(99, 101);
    std::uniform_int_distribution<int> qtyDist  (1,  10);
    std::uniform_int_distribution<int> sideDist (0,   1);

    std::vector<Order> orders;
    orders.reserve(numOrders);
    for (int i = 0; i < numOrders; ++i)
        orders.push_back({ i, priceDist(rng), qtyDist(rng), sideDist(rng) == 1, false });

    auto t0 = std::chrono::high_resolution_clock::now();
    for (auto& o : orders) addOrder(o);
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms  = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double ops = numOrders / (ms / 1000.0);

    std::cout << "  Orders processed: " << numOrders     << "\n";
    std::cout << "  Trades executed:  " << trades.size() << "\n";
    std::cout << "  Time:             " << std::fixed << std::setprecision(2) << ms  << " ms\n";
    std::cout << "  Throughput:       " << std::fixed << std::setprecision(0) << ops << " orders/sec\n";
    std::cout << "===========================\n\n";

    silent = false;
}

// ─────────────────────────────────────────────
//  reset  —  wipe all book state
// ─────────────────────────────────────────────
void OrderBook::reset() {
    bids.clear();
    asks.clear();
    orderLookup.clear();
    trades.clear();
}

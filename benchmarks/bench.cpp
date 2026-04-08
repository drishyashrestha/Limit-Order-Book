// ─────────────────────────────────────────────
//  bench.cpp
//  Standalone benchmark: 100k and 1M randomised
//  limit orders, pure matching throughput (no I/O).
//
//  Build + run:  make bench
// ─────────────────────────────────────────────

#include "../include/order_book.hpp"
#include <iostream>

int main() {
    std::cout << "=== LOB THROUGHPUT BENCHMARK ===\n";

    OrderBook book;
    book.benchmark(100'000);
    book.benchmark(1'000'000);

    return 0;
}

# ─────────────────────────────────────────────
#  Makefile — Limit Order Book Engine
#
#  Targets:
#    make          build demo binary  →  ./lob
#    make test     build + run unit tests  (exit 1 on failure)
#    make bench    build + run benchmark   (100k + 1M orders)
#    make clean    remove all build artifacts
# ─────────────────────────────────────────────

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I include

SRC   := src/order_book.cpp
MAIN  := src/main.cpp
TESTS := tests/test_order_book.cpp
BENCH := benchmarks/bench.cpp

.PHONY: all test bench clean

all: lob

lob: $(MAIN) $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "Built ./lob — run with: ./lob"

test: $(TESTS) $(SRC)
	$(CXX) $(CXXFLAGS) -o test_runner $^
	./test_runner
	@rm -f test_runner

bench: $(BENCH) $(SRC)
	$(CXX) $(CXXFLAGS) -o bench_runner $^
	./bench_runner
	@rm -f bench_runner

clean:
	rm -f lob lob.exe bench_runner bench_runner.exe test_runner test_runner.exe *.o
	@echo "Clean."

CXX = g++
CXXFLAGS = -std=c++17 -O2 -pthread -Wall -Wextra
TESTS = hash_map_test
BENCH = benchmark
HEADERS = concurrent_hash_map.h lock_free_hash_map.h lock_free_dynamic_resize_hash_map.h lock_free_open_addressing_hash_map.h

all: $(TESTS) $(BENCH)

$(TESTS): tests.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(TESTS) tests.cpp

$(BENCH): benchmark.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $(BENCH) benchmark.cpp

test: $(TESTS)
	./$(TESTS)

bench: $(BENCH)
	./$(BENCH)

clean:
	rm -f $(TESTS) $(BENCH)

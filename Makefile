# Detect OS
UNAME_S := $(shell uname -s)

# Targets
TARGETS = benchmark_compare test_phase1 test_library_correctness collatz_memo word_counter parallel_bfs resource_cache
EXECUTABLES = $(TARGETS)

ifeq ($(UNAME_S), Darwin)
    # macOS Configuration (Local)
    CXX = clang++
    CXXFLAGS = -std=c++17 -O3 -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include -I/opt/homebrew/include -DUSE_TBB
    LDFLAGS = -L/opt/homebrew/opt/libomp/lib -L/opt/homebrew/lib -lomp -ltbb
else
    # Linux Configuration (crunchy5 - Local TBB in ~/libs/tbb)
    CXX = g++
    # Point to the local TBB installation in the user's home directory
    TBB_DIR = $(HOME)/libs/tbb
    CXXFLAGS = -std=c++17 -O3 -fopenmp -Wall -DUSE_TBB -I$(TBB_DIR)/include
    LDFLAGS = -L$(TBB_DIR)/lib/intel64/gcc4.8 -ltbb -Wl,-rpath,$(TBB_DIR)/lib/intel64/gcc4.8
endif

all: $(TARGETS)

test_phase1: test_phase1.cpp naive_map.h
	$(CXX) $(CXXFLAGS) -o test_phase1 test_phase1.cpp $(LDFLAGS)

benchmark_compare: benchmark_compare.cpp naive_map.h concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o benchmark_compare benchmark_compare.cpp $(LDFLAGS)

test_library_correctness: test_library_correctness.cpp concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o test_library_correctness test_library_correctness.cpp $(LDFLAGS)

collatz_memo: collatz_memo.cpp naive_map.h concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o collatz_memo collatz_memo.cpp $(LDFLAGS)

word_counter: word_counter.cpp naive_map.h concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o word_counter word_counter.cpp $(LDFLAGS)

parallel_bfs: parallel_bfs.cpp naive_map.h concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o parallel_bfs parallel_bfs.cpp $(LDFLAGS)

resource_cache: resource_cache.cpp naive_map.h concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o resource_cache resource_cache.cpp $(LDFLAGS)

clean:
	rm -f $(TARGETS)

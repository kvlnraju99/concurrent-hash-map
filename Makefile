# Default Compiler for Linux / CIMS
CXX = g++
CXXFLAGS = -std=c++17 -O3 -fopenmp -Wall
LDFLAGS = 

# Uncomment these lines if running on macOS with Homebrew libomp
# CXX = clang++
# CXXFLAGS = -std=c++17 -O3 -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include
# LDFLAGS = -L/opt/homebrew/opt/libomp/lib -lomp

TARGETS = test_phase1 benchmark_compare

all: $(TARGETS)

test_phase1: test_phase1.cpp naive_map.h
	$(CXX) $(CXXFLAGS) -o test_phase1 test_phase1.cpp $(LDFLAGS)

benchmark_compare: benchmark_compare.cpp naive_map.h concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o benchmark_compare benchmark_compare.cpp $(LDFLAGS)

clean:
	rm -f $(TARGETS)

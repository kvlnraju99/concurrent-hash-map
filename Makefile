CXX      = g++
CXXFLAGS = -std=c++17 -pthread -Wall -Wextra -Wno-unused-lambda-capture
TESTS    = hash_map_test
BENCH    = benchmark
PROFILE  = profile_runner
SWEEP    = bucket_sweep

all: $(TESTS) $(BENCH) $(PROFILE) $(SWEEP)

$(TESTS): tests.cpp concurrent_hash_map.h lock_free_hash_map.h lock_free_open_addressing_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(TESTS) tests.cpp

$(BENCH): benchmark.cpp concurrent_hash_map.h lock_free_hash_map.h lock_free_open_addressing_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(BENCH) benchmark.cpp

$(PROFILE): profile.cpp lock_free_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(PROFILE) profile.cpp

$(SWEEP): bucket_sweep.cpp lock_free_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(SWEEP) bucket_sweep.cpp

test: $(TESTS)
	./$(TESTS)

bench: $(BENCH)
	./$(BENCH)

profile: $(PROFILE)
	./$(PROFILE)

sweep: $(SWEEP)
	./$(SWEEP)

clean:
	rm -f $(TESTS) $(BENCH) $(PROFILE) $(SWEEP)

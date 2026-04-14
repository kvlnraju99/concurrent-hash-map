CXX      = g++
CXXFLAGS = -std=c++17 -pthread -Wall -Wextra -Wno-unused-lambda-capture
TARGET   = hash_map_test
BENCH    = benchmark
PROFILE  = profile_runner

all: $(TARGET) $(BENCH) $(PROFILE)

$(TARGET): main.cpp concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp

$(BENCH): benchmark.cpp concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(BENCH) benchmark.cpp

$(PROFILE): profile.cpp lock_free_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(PROFILE) profile.cpp

run: $(TARGET)
	./$(TARGET)

bench: $(BENCH)
	./$(BENCH)

profile: $(PROFILE)
	./$(PROFILE)

clean:
	rm -f $(TARGET) $(BENCH) $(PROFILE)

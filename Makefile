CXX      = g++
CXXFLAGS = -std=c++17 -pthread -Wall -Wextra -Wno-unused-lambda-capture
TARGET   = hash_map_test
BENCH    = benchmark

all: $(TARGET) $(BENCH)

$(TARGET): main.cpp concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp

$(BENCH): benchmark.cpp concurrent_hash_map.h
	$(CXX) $(CXXFLAGS) -O2 -o $(BENCH) benchmark.cpp

run: $(TARGET)
	./$(TARGET)

bench: $(BENCH)
	./$(BENCH)

clean:
	rm -f $(TARGET) $(BENCH)

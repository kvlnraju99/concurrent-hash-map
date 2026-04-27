# Default Compiler for Linux / CIMS
CXX = g++
CXXFLAGS = -std=c++17 -O3 -fopenmp -Wall
LDFLAGS = 

# Uncomment these lines if running on macOS with Homebrew libomp
# CXX = clang++
# CXXFLAGS = -std=c++17 -O3 -Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include
# LDFLAGS = -L/opt/homebrew/opt/libomp/lib -lomp

TARGET = test_phase1

all: $(TARGET)

$(TARGET): test_phase1.cpp naive_map.h
	$(CXX) $(CXXFLAGS) -o $(TARGET) test_phase1.cpp $(LDFLAGS)

clean:
	rm -f $(TARGET)

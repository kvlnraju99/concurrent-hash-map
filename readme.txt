Concurrent Hash Map Project
Raju Kanumuri (vk2646)

Files to use:
- concurrent_hash_map.h
- lock_free_hash_map.h
- lock_free_dynamic_resize_hash_map.h
- lock_free_open_addressing_hash_map.h
- tests.cpp
- benchmark.cpp
- Makefile
- report/report.docx

Compile:
make

Run all tests:
./hash_map_test

Run tests for one implementation:
./hash_map_test locked
./hash_map_test lockfree
./hash_map_test resize
./hash_map_test open

Run benchmark:
./benchmark --threads 8 --ops 100000 --buckets 131072

Notes:
- The benchmark compares all four implementations in one executable.
- The lock-free open-addressing implementation is fixed-size, so the benchmark keeps the key space below capacity.

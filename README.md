# Concurrent Hash Map Project

This submission keeps all four implementations in one branch:

- `concurrent_hash_map.h` - lock-based hash map with dynamic resizing
- `lock_free_hash_map.h` - fixed-size lock-free chaining hash map
- `lock_free_dynamic_resize_hash_map.h` - lock-free chaining hash map with cooperative resizing
- `lock_free_open_addressing_hash_map.h` - open-addressing experiment

Main files:

- `tests.cpp` - correctness tests for all four implementations
- `benchmark.cpp` - throughput benchmark for all four implementations
- `scripts/capture_benchmark_results.py` - saves benchmark output to report-ready text, CSV, and JSON files
- `Makefile` - build targets
- `readme.txt` - quick compile and run instructions
- `report/report.docx` - final report

## Build

```bash
make
```

## Run tests

```bash
./hash_map_test
```

Run one implementation only:

```bash
./hash_map_test locked
./hash_map_test lockfree
./hash_map_test resize
./hash_map_test open
```

## Run benchmark

```bash
./benchmark --threads 8 --ops 100000 --buckets 131072
```

The benchmark keeps the active key space at `min(ops, buckets / 4)` so the open-addressing implementation stays below capacity.

## Capture report benchmark data

```bash
python3 scripts/capture_benchmark_results.py --threads 8 --ops 800000 --buckets 131072
```

This saves:

- `report/data/final_benchmark.txt`
- `report/data/final_benchmark.csv`
- `report/data/final_benchmark.json`

## Clean binaries

```bash
make clean
```

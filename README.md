# Concurrent Hash Map

This repository contains two C++ hash map implementations:

- `ConcurrentHashMap` in `/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/concurrent_hash_map.h`
  - bucket-level locking
  - dynamic resizing
- `LockFreeHashMap` in `/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/lock_free_hash_map.h`
  - fixed bucket count
  - lock-free insert/get/remove traversal with eager unlinking of deleted nodes
  - optional software profiling for `put()`

## Files

- `tests.cpp` - correctness tests for locked and lock-free maps
- `benchmark.cpp` - side-by-side throughput benchmark
- `profile.cpp` - lock-free `put()` micro-profiler
- `bucket_sweep.cpp` - bucket-count sweep for `put/get/remove/miss`
- `scripts/variant_sweep.py` - cross-branch sweep for all four major map variants
- `scripts/extended_experiments.py` - focused follow-up experiments for workload mix, contention, and resizing
- `Makefile` - build and run targets

## Build

```bash
make
```

## Run

Tests:

```bash
./hash_map_test
./hash_map_test locked
./hash_map_test lockfree
```

Benchmark:

```bash
./benchmark --threads 64 --ops 800000 --buckets 524288
```

PUT micro-profile:

```bash
./profile_runner --threads 64 --ops-per-thread 5000 --single-bucket-ops 1000 --wide-buckets 131072 --hot-key-space 64
```

Bucket sweep:

```bash
./bucket_sweep --threads 64 --ops-per-thread 5000 --raw
./bucket_sweep --threads 64 --ops-per-thread 5000 --buckets 32768,65536,131072,262144
```

Cross-variant sweep:

```bash
python3 ./scripts/variant_sweep.py
```

This creates two CSV files under `/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/results`:

- `variant_sweep_raw.csv` - every run
- `variant_sweep_summary.csv` - median/mean/best per scenario

Default matrix:

- variants: locked dynamic, fixed-size lock-free chaining, dynamic-resizing lock-free chaining, open-addressing experiment
- workloads: `put`, `get`, `mixed`
- threads: `1,2,4,8,16`
- buckets: `65536,262144,1048576`
- open-addressing-safe load: `key_space = buckets * 0.25`

Focused follow-up experiments:

```bash
python3 ./scripts/extended_experiments.py
```

This creates additional CSV files under `/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/results`:

- `workload_mix_raw.csv` / `workload_mix_summary.csv`
- `contention_sweep_raw.csv` / `contention_sweep_summary.csv`
- `resize_focus_raw.csv` / `resize_focus_summary.csv`

These experiments target three report questions:

- workload mix: how the variants behave under read-heavy, balanced, and write-heavy traffic
- contention sweep: how performance changes when many threads fight over the same key space
- resize focus: whether dynamic resizing helps when the table starts small and must grow

## Make targets

```bash
make test
make bench
make profile
make sweep
make clean
```

## Notes

- The lock-free map does not implement dynamic resizing.
- The lock-free profiler is software-level instrumentation, not hardware counter profiling.
- The single-bucket profiling scenario is intentionally worst-case; keep `--single-bucket-ops` smaller for quick runs.

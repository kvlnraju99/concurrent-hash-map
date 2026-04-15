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

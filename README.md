# Concurrent Hash Map Library for OpenMP

A high-performance, thread-safe Hash Map library designed for Shared Memory Parallelism using OpenMP. This project demonstrates the evolution from a naive global-lock implementation to a sophisticated, dynamically-resizing concurrent data structure.

## 🚀 Library Architecture

The project implements three versions of the hash map to demonstrate synchronization trade-offs:

1.  **V1: Naive Global Lock** - Uses a single `std::mutex` for the entire map. High contention bottleneck.
2.  **V2: Fine-Grained Static** - Implements a "Lock-per-Bucket" strategy. Allows simultaneous access to different buckets.
3.  **V3: Dynamic Concurrent** - Adds **Dynamic Resizing** using a `std::shared_mutex` (Reader-Writer lock) for the global structure. Automatically grows to maintain $O(1)$ performance.

### Key Technical Features
*   **Atomic Updates**: Supports `update(key, lambda)` for thread-safe Read-Modify-Write operations.
*   **Shared Locks**: Uses Reader-Writer locks to allow multiple threads to read/get simultaneously.
*   **Thread-Safe Growth**: Resizing occurs in the background without losing data or blocking all threads indefinitely.

---

## 📊 Performance Benchmarks (32 Threads)

The following results were collected on a 64-core Linux environment (`crunchy5`).

| Application | Naive (Global) | Library V2 (Static) | Library V3 (Dynamic) | Speedup (V2 vs Naive) |
| :--- | :--- | :--- | :--- | :--- |
| **Word Counter** | 1.96s | 0.14s | 0.42s | **14.0x** |
| **Parallel BFS** | 7.13s | 0.11s | 1.92s | **64.8x** |
| **Resource Cache** | 4.36s | 0.06s | 1.13s | **72.6x** |
| **Collatz Memo** | 10.55s | 2.96s | 5.46s | **3.5x** |

### Key Observations:
1.  **The Contention Bottleneck**: In the Resource Cache test, the Naive version actually got **5x slower** as threads increased from 1 to 32, proving that global locks do not scale.
2.  **The "Starvation" Robustness**: When the initial bucket count was set to 100 for 100k nodes, **V3 (Dynamic)** outperformed **V2 (Static)** by **11x** because it could self-correct through resizing.
3.  **Fine-Grained Efficiency**: In graph traversal (BFS), the library achieved a massive **64x speedup**, demonstrating the power of bucket-level locking in unpredictable workloads.

---

## 🛠 Application Suite

1.  **Shared Collatz Memoization**: Demonstrates computation-heavy workloads and shared caching.
2.  **Parallel Word Counter**: Uses atomic updates to count frequencies in a Zipfian distribution corpus.
3.  **Parallel Graph BFS**: A level-synchronous graph traversal using the map as a "Visited" set.
4.  **Web Browser Resource Cache**: A mixed-workload simulation (20% Writers / 80% Readers).

---

## ⚙️ How to Build and Run

### Prerequisites
*   C++17 Compiler (GCC or Clang)
*   OpenMP Library

### Compilation
```bash
make clean
make
```

### Running Benchmarks
```bash
./word_counter 1000000 10000 32
./parallel_bfs 100000 20 32
./resource_cache 2000000 32
./collatz_memo 1000000 32
```

## ✅ Verification
All applications include a verification phase that confirms results against a serial baseline or mathematical invariants (e.g., total word sum, visited node count). 
*   **Result**: All tests PASSED with 0 errors across millions of concurrent operations.

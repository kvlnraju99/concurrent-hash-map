# Presentation: High-Performance Concurrent Hash Maps for OpenMP

## Slide 1: Title Slide
**Title:** High-Performance Concurrent Hash Maps for OpenMP  
**Subtitle:** From Global Locks to Wait-Free Readers  
**Name:** [Your Name]  
**Course:** Semester 4 Multi-Core Programming  

---

## Slide 2: The Challenge
*   **The Problem:** Standard C++ containers (`std::unordered_map`) are not thread-safe.
*   **The Traditional Solution:** Wrap the map in a global mutex.
*   **The Bottleneck:** On a 64-core machine like `crunchy5`, global locks lead to massive contention, often making parallel code slower than serial execution.
*   **Objective:** Build a library that scales linearly with thread count.

---

## Slide 3: The Evolutionary Path
We developed 5 versions of the library to explore the synchronization spectrum:
1.  **V1: Global Lock (Baseline)** - Simple, safe, but slow.
2.  **V2: Fine-Grained Static** - Lock-per-bucket strategy.
3.  **V3: Dynamic Concurrent** - Adds adaptive resizing with Reader-Writer locks.

---

## Slide 4: Deep Dive - Lock-per-Bucket (V2)
*   **Concept:** Instead of one lock for the whole map, we give each bucket its own mutex.
*   **Advantage:** Multiple threads can insert/get simultaneously as long as they hash to different buckets.
*   **Result:** Drastically reduces contention in uniform workloads.

---

## Slide 5: Adaptive Resizing (V3)
*   **The Problem:** If the number of elements grows, buckets get long, and $O(1)$ becomes $O(N)$.
*   **The Solution:** Use a `std::shared_mutex` for the "Global Structure."
    *   **Readers:** Take a `shared_lock` (simultaneous).
    *   **Writers (New Keys):** Take a `unique_lock`.
    *   **Resizing:** When the load factor is exceeded, the map doubles its buckets in the background.

---

## Slide 6: State-of-the-Art - RCU & Wait-Free (V5)
*   **Innovation:** Eliminate the shared mutex for readers entirely.
*   **Mechanism:**
    *   **Active Reader Counter:** Tracks threads currently accessing the map.
    *   **RCU Swap:** During resize, a new bucket array is built and "swapped" in atomically.
    *   **The Wait:** The writer waits for existing readers to finish before deleting the old array.
*   **Impact:** Readers are never blocked by other readers or even by the resize process in progress.

---

## Slide 7: The Benchmark Suite
To test compliance with project requirements, we selected 4 programs with distinct characteristics:
1.  **Word Counter:** High-contention updates (Zipfian hotspots).
2.  **Parallel BFS:** Irregular memory access & graph synchronization.
3.  **Resource Cache:** Read-heavy workload (80/20 split).
4.  **Collatz Memoization:** Computation-intensive with shared caching.

---

## Slide 8: Application Spotlight - Word Counter
*   **The Test:** Count frequencies of 1,000,000 words in a Zipf-distributed corpus.
*   **Why it matters:** Tests the `update(key, lambda)` performance.
*   **Early Insight:** V2/V3 showed up to **14x speedup** over Global locks by allowing concurrent updates to different word buckets.

---

## Slide 9: Application Spotlight - Parallel BFS
*   **The Test:** Breadth-First Search on a 100,000-node random graph.
*   **Map Usage:** Tracking "Visited" nodes.
*   **Performance:** Achieved a massive **64x speedup** in initial 32-thread tests, proving the library handles irregular graph traversal sync efficiently.

---

## Slide 10: Preliminary Results (32 Threads)
*(Data collected from `crunchy5` baseline)*

| Application | Naive (Global) | Library V2/V3 | Speedup |
| :--- | :--- | :--- | :--- |
| Word Counter | 1.96s | 0.14s | **14x** |
| Parallel BFS | 7.13s | 0.11s | **64x** |
| Resource Cache | 4.36s | 0.06s | **72x** |
| Collatz Memo | 10.55s | 2.96s | **3.5x** |

---

## Slide 11: Scaling to 64 Cores (Upcoming)
*   **The Goal:** Analyze performance on a full 64-core `crunchy5` node.
*   **Hypothesis:** V5 (Wait-Free) will significantly outperform V3 (Locked) as thread count increases and contention for the global shared mutex becomes the new bottleneck.
*   **Analysis:** We will measure speedup, efficiency, and throughput (Ops/sec).

---

## Slide 12: Conclusion
*   **Success:** Demonstrated that data structure design is as critical as parallel algorithm design.
*   **Key Takeaway:** Fine-grained locking and RCU-style patterns are essential for modern multi-core scalability.
*   **Future Work:** Porting the library to distributed memory (MPI) or GPU (CUDA).

# Presentation: High-Performance Concurrent Hash Maps for OpenMP

## Slide 1: Title Slide

**Title:** Concurrent Hash Maps for Multi-Core Scaling
**Name:** Raju Kanumuri
**Course:** Semester 4 Multi-Core Programming

---

## Slide 2: Problem Definition & Importance

- **The Problem:** Standard C++ containers are not thread-safe. Protecting them with a **Global Mutex** turns a 64-core server into a sequential processor.
- **The Challenge:** Design a hash map that achieves sustainable scaling by minimizing "Lock Contention" and "Wait Time" during high-frequency operations.
- **Why It Matters:** High-concurrency maps are the backbone of modern web servers (caches), parallel graph processing (BFS), and database engines (like Redis).

---

## Slide 3: Survey & Background

- **Sequential Baseline:** C++ std::unordered_map (Standard hashing with bucket-chaining).
- **State of the Art:** Intel TBB (concurrent_hash_map). Uses a segmented architecture and reader-writer locks to achieve high concurrency.

---

## Slide 4: Proposed Ideas & Implementation

We evolved the library through three major architectural shifts:

1.  **Fine-Grained Static:** Fixed array of buckets with individual lock per bucket. Zero global contention but cannot grow as the load factor increases.
2.  **Dynamic Resizing:** Added adaptive resizing and rehashing. Used a global flag to pause operations during resizing and cache line alignment to reduce false sharing between cores.
3.  **Segmentation + Lock-Free Reads:**
    - **Segmentation:** The map is split into independent segments. One segment resizes without blocking others.
    - **Lock-Free Traversal:** `get()` uses atomic pointer swapping, allowing reads without any mutex overhead.

---

## Slide 5: Experimental Setup

- **Hardware:** 64-Core Linux Server (`crunchy5`).
- **Benchmarks:**
  - **Word Counter:** Scans a large text to count the frequency of unique words. (Update-Heavy: High lock contention as threads frequently increment the same keys).
  - **Resource Cache:** Simulates a web browser looking up URLs in a shared memory cache. (Read-Intensive: Tests maximum retrieval throughput with minimal writing).
  - **Parallel BFS:** A graph traversal that explores nodes in layers, using the map to mark "visited" nodes. (Communication-Heavy: Threads must constantly sync to avoid re-exploring nodes).
  - **Collatz Memoization:** Calculates mathematical $3n+1$ sequences, using the map to store and reuse intermediate results. (Computation-Intensive: High frequency of small "check-and-insert" operations).
- **Comparison:** Side-by-side comparison with **Intel TBB** and Sequential C++.

---

## Slide 6: Results & Analysis (1) - The Wins

- **Word Counter:** V6 Scalability is superior. At 64 threads, V6 (0.088s) outperforms Intel TBB (0.129s).
- **Resource Cache:** The **Lock-Free Read** optimization in V6 makes it 3x faster than Intel TBB at peak concurrency (0.047s vs 0.163s).
- **Scaling Pattern:** While V3 "plateaus" after 32 threads due to global resize contention, V6 continues to scale linearly up to 64 cores.

---

## Slide 7: Results & Analysis (2) - The Trade-offs

- **The "Segmenting Tax":** In Parallel BFS and Collatz, V6 was actually **slower** than V2 (Fine-grained static).
  - **V2 BFS:** 0.48s
  - **V6 BFS:** 1.51s
- **Reasoning:** V6 requires "Double Hashing" (segment then bucket) and extra pointer indirections. For lightweight operations where computation is small, segmentation overhead outweighs concurrency benefits.

---

## Slide 8: Conclusions & Takeaways

1.  **Lock-Free Reads are the Single Best Optimization:** Removing mutexes from the `get()` path provided a massive performance boost in read-heavy simulation.
2.  **Segmentation is Mandatory for 64+ Cores:** "Stop-the-world" resizing is acceptable for small thread counts but fails on server-grade hardware.
3.  **Complexity vs. Performance:** There is no "perfect" map. Use **V6** for high-throughput server workloads, and **V2** for low-latency parallel algorithms with fixed-size data.

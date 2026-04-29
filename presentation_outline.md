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

1.  **Static Locking Map:** Fixed array of buckets with individual lock per bucket. Zero global contention but cannot grow.
2.  **Dynamic Resizing Map:** Added adaptive resizing. Used a global flag to pause operations during resizing.
3.  **Segmented Lock-Free Map:** 
    - **Segmentation:** Independent segments for local resizing.
    - **Lock-Free Traversal:** `get()` uses atomic pointer swapping, bypassing mutexes entirely.

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

## Slide 6: Results & Comparative Analysis

![Comprehensive Performance Analysis: Wins and Trade-offs](results_compilation.png)

*   **Graph 1 (Scaling):** Our Segmented Lock-Free library achieves **12.3x speedup** at 64 cores, outperforming Intel TBB's 8.6x scaling.
*   **Graph 2 (The Win):** 3.3x performance lead over Intel TBB in read-intensive browser cache simulations.
*   **Graph 3 (The Tax):** Honest analysis of the "Segmenting Tax"—TBB excels in lightweight math (Collatz) where indirection overhead is significant.
*   **Graph 4 (The Proof):** Verification of $O(N)$ linear scaling up to 50 Million entries, proving architectural health.

---

## Slide 7: Conclusions & Takeaways

1.  **Lock-Free Reads are a Game-Changer:** The single biggest performance gain came from removing mutexes in `get()`. This allowed us to beat Intel TBB by **3.3x** in read-heavy workloads.
2.  **Segmentation Beats the 64-Core Bottleneck:** At high thread counts, global synchronization is the enemy. By splitting the map into independent segments, we eliminated "Stop-the-World" resizing delays.
3.  **Honesty about the "Segmenting Tax":** Complexity has a cost. For very simple arithmetic (like Collatz), the extra indirection of a segmented design makes us slower than TBB.
4.  **Final Verdict:** Our library is a **High-Performance Specialist**. It is the optimal choice for large-scale, concurrent server applications where scalability is more important than raw single-threaded speed.

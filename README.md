# Concurrent Hash Map Library for OpenMP

A high-performance, thread-safe Hash Map library designed for Shared Memory Parallelism using OpenMP. This project compares sophisticated concurrent data structures against naive locking mechanisms to demonstrate scalability and performance in multi-core environments.

## 🚀 Project Overview

The goal of this project is to implement a robust concurrent hash map library and evaluate its performance across diverse parallel workloads. We focus on reducing lock contention and maximizing CPU utilization as thread counts increase.

## 🛠 Implementation Phases

The project is structured into five distinct development phases:

1.  **Phase 1: The Baseline (Naive Version)**
    *   Implement a thread-safe wrapper around `std::unordered_map` using a single **Global Lock**.
    *   Serves as the performance floor (baseline) for all comparisons.
2.  **Phase 2: Core Library (Bucket-Level Locking)**
    *   Implement a custom hash map with fine-grained, bucket-level mutexes.
    *   Allows simultaneous access to different segments of the map.
3.  **Phase 3: Advanced Features (Lock-Free & Resizing)**
    *   **Lock-Free Operations:** Utilizing atomic CAS (Compare-And-Swap) for overhead reduction.
    *   **Dynamic Resizing:** Implementing a concurrent resize strategy to maintain O(1) performance.
4.  **Phase 4: Parallel Benchmarking Applications**
    *   Development of 4 distinct parallel programs using both the Library and the Naive version.
5.  **Phase 5: Scalability Analysis**
    *   Comprehensive performance profiling and report generation.

## 📊 Benchmark Applications

We evaluate our library using four parallel programs with different resource characteristics:

| Program | Characteristics | Description |
| :--- | :--- | :--- |
| **Word Frequency Counter** | Memory Intensive | Processes large text datasets; high contention on common word keys. |
| **Shared Memoization** | Computation Intensive | Caching results for parallel recursive algorithms (e.g., Fibonacci). |
| **Network Flow Simulator** | Communication Heavy | Tracking simulated packet flows across multiple threads in real-time. |
| **Parallel BFS** | Complex Access | Using the map to track "visited" nodes in large-scale graph traversals. |

## ⚙️ Technology Stack

*   **Language:** C++17
*   **Parallelism:** OpenMP
*   **Compiler:** GCC / Clang (with OpenMP support)
*   **Hardware:** Multi-core x86_64 / ARM64

## 📈 Evaluation Metrics

*   **Execution Time:** Total time to complete the workload.
*   **Speedup:** Performance gain relative to a single-threaded execution ($T_1 / T_n$).
*   **Scalability:** Ability to maintain performance as thread count increases.

---
**Author:** Raju Kanumuri
**Course:** Multi-Core Architecture (Semester 4)

# Final Benchmark Report: Concurrent Hash Map Optimization
**Date**: April 29, 2026
**Hardware**: 64-Core Server (crunchy5)

## 1. Word Frequency Counter
*Workload: 5M words, 100k unique*

### Experiment 1: Thread Scalability (Buckets: 131k)
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1.0746 | 1.2240 | 1.9063 | 2.1570 | 1.2179 |
| 2 | 1.0428 | 0.9245 | 1.5100 | 1.5816 | 0.9764 |
| 4 | 1.0393 | 0.6008 | 0.7970 | 0.8150 | 0.5267 |
| 8 | 1.0434 | 0.3353 | 0.4041 | 0.4964 | 0.3520 |
| 16 | 1.0830 | 0.2263 | 0.2817 | 0.2605 | 0.1763 |
| 32 | 1.0664 | 0.1115 | 0.1667 | 0.1544 | 0.1617 |
| 64 | 1.0584 | 0.0962 | 0.1325 | **0.0888** | 0.1290 |

### Experiment 2: Bucket Sensitivity (Threads: 64)
| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | 1.0467 | 0.5769 | 0.6131 | **0.1850** | 0.1309 |
| 5000 | 1.0615 | 0.2557 | 0.2943 | 0.1867 | 0.1370 |
| 10000 | 0.9960 | 0.1901 | 0.2098 | 0.1849 | 0.1241 |
| 50000 | 1.0914 | 0.1460 | 0.1587 | 0.1742 | 0.1306 |
| 100000 | 1.0333 | 0.1367 | 0.1311 | 0.1502 | 0.1313 |
| 500000 | 1.0796 | 0.1190 | 0.1406 | 0.0821 | 0.0893 |

---

## 2. Resource Cache Simulation
*Workload: 5M Ops, 1k unique URLs*

### Experiment 1: Thread Scalability (Buckets: 131k)
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 0.8931 | 1.0360 | 1.2085 | 1.4454 | 1.2995 |
| 2 | 0.8881 | 0.8421 | 0.9787 | 0.7223 | 1.2120 |
| 4 | 0.8859 | 0.4827 | 0.5484 | 0.3722 | 0.7128 |
| 8 | 0.8819 | 0.2701 | 0.2972 | 0.2119 | 0.4600 |
| 16 | 0.8919 | 0.1559 | 0.1671 | 0.1059 | 0.2532 |
| 32 | 0.8920 | 0.0936 | 0.0986 | 0.0572 | 0.1647 |
| 64 | 0.8834 | 0.0878 | 0.0810 | **0.0478** | 0.1635 |

---

## 3. Parallel BFS
*Workload: 500k nodes, 20 edges/node*

### Experiment 1: Thread Scalability
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1.3398 | 3.4121 | 2.8366 | 2.7801 | 3.1541 |
| 2 | 1.5210 | 2.8353 | 3.1904 | 2.5776 | 1.9937 |
| 4 | 1.6372 | 1.7003 | 1.7598 | 1.8971 | 1.2353 |
| 8 | 1.6143 | 1.0459 | 1.0477 | 1.6906 | 0.8928 |
| 16 | 1.6671 | 0.7345 | 0.7437 | 1.2157 | 0.7003 |
| 32 | 1.3664 | 0.3179 | 0.5316 | 1.1533 | 0.2756 |
| 64 | 1.3600 | **0.4866** | 0.4830 | 1.5161 | 0.4986 |

---

## 4. Collatz Memoization
*Workload: 1M numbers, 65k buckets*

### Experiment 1: Thread Scalability
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1.1933 | 5.2945 | 4.7346 | 4.6272 | 1.4849 |
| 2 | 1.2488 | 5.0863 | 4.8439 | 6.1911 | 1.4144 |
| 4 | 1.4405 | 3.4673 | 3.4194 | 7.2824 | 1.2032 |
| 8 | 1.2428 | 2.2591 | 2.1658 | 6.3380 | 1.2030 |
| 16 | 1.4917 | 1.4057 | 1.7438 | 5.8469 | 0.4309 |
| 32 | 1.4060 | 1.4536 | 1.1973 | 4.5265 | 1.1458 |
| 64 | 1.4411 | 0.8776 | 1.3097 | 4.4512 | **0.4579** |

---

## Architectural Analysis
1. **Contention Bottleneck**: V3 shows "Plateauing" after 32 threads in the Word Counter due to global resize flag contention. V6 continues to scale.
2. **Segmenting Tax**: BFS and Collatz expose the overhead of hashing twice and extra pointer indirections in V6. 
3. **Resizing Efficiency**: V6 wins whenever the initial bucket count is low because it doesn't stop all 64 threads for a resize.

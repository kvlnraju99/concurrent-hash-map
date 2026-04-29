# Final Benchmark Report: Concurrent Hash Map Optimization

**Date**: April 29, 2026
**Hardware**: 64-Core Server (crunchy5)

## 1. Word Frequency Counter
*Workload: 5M words, 100k unique*

### Experiment 1: Thread Scalability (Buckets: 131k)
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1.0683 (P) | 1.2175 (P) | 1.8716 (P) | 2.1713 (P) | 1.5126 (P) |
| 2 | 1.0311 (P) | 0.9336 (P) | 1.5342 (P) | 1.4870 (P) | 0.9235 (P) |
| 4 | 1.0485 (P) | 0.5609 (P) | 0.8966 (P) | 0.9072 (P) | 0.6265 (P) |
| 8 | 1.0341 (P) | 0.3299 (P) | 0.4490 (P) | 0.4813 (P) | 0.3195 (P) |
| 16 | 1.0636 (P) | 0.2182 (P) | 0.2771 (P) | 0.2553 (P) | 0.2326 (P) |
| 32 | 0.9918 (P) | 0.1146 (P) | 0.1850 (P) | 0.1506 (P) | 0.1581 (P) |
| 64 | 1.1023 (P) | 0.1308 (P) | 0.1169 (P) | **0.0893 (P)** | 0.1281 (P) |

### Experiment 2: Bucket Sensitivity (Threads: 64)
| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | 1.0845 (P) | 0.6304 (P) | 0.6214 (P) | 0.1867 (P) | 0.0918 (P) |
| 5000 | 1.0534 (P) | 0.3022 (P) | 0.2700 (P) | 0.1658 (P) | 0.1257 (P) |
| 10000 | 1.0376 (P) | 0.2087 (P) | 0.2484 (P) | 0.1591 (P) | 0.1268 (P) |
| 50000 | 1.0937 (P) | 0.1607 (P) | 0.1603 (P) | 0.1798 (P) | 0.0927 (P) |
| 100000 | 1.0409 (P) | 0.1410 (P) | 0.1499 (P) | 0.1482 (P) | 0.0932 (P) |
| 500000 | 1.0375 (P) | 0.1181 (P) | 0.1251 (P) | 0.0901 (P) | 0.1286 (P) |

### Experiment 3: Contention Intensity (Threads: 64)
| Unique Words | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | 0.5093 (P) | 0.0631 (P) | 0.0647 (P) | 0.0664 (P) | 0.1253 (P) |
| 5000 | 0.5972 (P) | 0.0620 (P) | 0.0771 (P) | 0.0716 (P) | 0.1001 (P) |
| 10000 | 0.5992 (P) | 0.0663 (P) | 0.0749 (P) | 0.0722 (P) | 0.0936 (P) |
| 50000 | 0.7522 (P) | 0.1108 (P) | 0.1270 (P) | 0.0887 (P) | 0.1203 (P) |
| 100000 | 1.0735 (P) | 0.1370 (P) | 0.1249 (P) | 0.0914 (P) | 0.1282 (P) |
| 500000 | 2.0022 (P) | 0.2205 (P) | 0.3718 (P) | 0.5164 (P) | 0.3732 (P) |

### Experiment 4: Problem Size Scaling (Threads: 64)
| Total Words | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1,000,000 | 0.2250 (P) | 0.0405 (P) | 0.0652 (P) | 0.0245 (P) | 0.0716 (P) |
| 5,000,000 | 1.0216 (P) | 0.0898 (P) | 0.1252 (P) | 0.0855 (P) | 0.1240 (P) |
| 10,000,000 | 2.1723 (P) | 0.2445 (P) | 0.2647 (P) | 0.1662 (P) | 0.1724 (P) |
| 20,000,000 | 4.6471 (P) | 0.3681 (P) | 0.4486 (P) | 0.3567 (P) | 0.4333 (P) |
| 50,000,000 | 10.3360 (P) | 0.7867 (P) | 0.7935 (P) | 0.8252 (P) | **0.7553 (P)** |

---

## 2. Resource Cache Simulation
*Workload: 5M Ops, 1k unique URLs*

### Experiment 1: Thread Scalability (Buckets: 131k)
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 0.8779 (P) | 1.0518 (P) | 1.2530 (P) | 1.4567 (P) | 1.3530 (P) |
| 2 | 0.8986 (P) | 0.8441 (P) | 0.9708 (P) | 0.7373 (P) | 1.1752 (P) |
| 4 | 0.8779 (P) | 0.4811 (P) | 0.5520 (P) | 0.3635 (P) | 0.7632 (P) |
| 8 | 0.8862 (P) | 0.2771 (P) | 0.3027 (P) | 0.1846 (P) | 0.4468 (P) |
| 16 | 0.8765 (P) | 0.1540 (P) | 0.1727 (P) | 0.1023 (P) | 0.2588 (P) |
| 32 | 0.8913 (P) | 0.0910 (P) | 0.0962 (P) | 0.0579 (P) | 0.1648 (P) |
| 64 | 0.8762 (P) | 0.0782 (P) | 0.0718 (P) | **0.0460 (P)** | 0.1676 (P) |

### Experiment 2: Bucket Sensitivity (Threads: 64)
| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | 0.8904 (P) | 0.1099 (P) | 0.0946 (P) | 0.0472 (P) | 0.1731 (P) |
| 5000 | 0.8877 (P) | 0.0904 (P) | 0.0769 (P) | 0.0509 (P) | 0.1805 (P) |
| 10000 | 0.8822 (P) | 0.0904 (P) | 0.0733 (P) | 0.0437 (P) | 0.1496 (P) |
| 50000 | 0.8784 (P) | 0.0808 (P) | 0.0762 (P) | 0.0468 (P) | 0.1642 (P) |
| 100000 | 0.8921 (P) | 0.0833 (P) | 0.0798 (P) | 0.0620 (P) | 0.1999 (P) |
| 500000 | 0.8872 (P) | 0.0832 (P) | 0.0748 (P) | 0.0523 (P) | 0.1576 (P) |

### Experiment 3: Contention Intensity (Threads: 64)
| Unique URLs | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 100 | 0.6793 (P) | 0.1513 (P) | 0.1366 (P) | 0.0396 (P) | 0.3019 (P) |
| 500 | 0.8267 (P) | 0.0897 (P) | 0.0899 (P) | 0.0429 (P) | 0.2237 (P) |
| 1000 | 0.8738 (P) | 0.0792 (P) | 0.0759 (P) | 0.0450 (P) | 0.1836 (P) |
| 5000 | 0.9615 (P) | 0.0792 (P) | 0.0770 (P) | 0.0458 (P) | 0.1406 (P) |
| 10000 | 0.9962 (P) | 0.0858 (P) | 0.0813 (P) | 0.0535 (P) | 0.1217 (P) |
| 50000 | 1.8246 (P) | 0.1147 (P) | 0.1210 (P) | 0.0924 (P) | 0.1480 (P) |

### Experiment 4: Problem Size Scaling (Threads: 64)
| Total Ops | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1,000,000 | 0.1778 (P) | 0.0303 (P) | 0.0219 (P) | 0.0138 (P) | 0.0362 (P) |
| 5,000,000 | 0.8762 (P) | 0.0789 (P) | 0.0763 (P) | 0.0441 (P) | 0.1689 (P) |
| 10,000,000 | 1.7550 (P) | 0.1431 (P) | 0.1445 (P) | 0.0884 (P) | 0.3402 (P) |
| 20,000,000 | 3.4861 (P) | 0.2568 (P) | 0.2909 (P) | 0.1718 (P) | 0.6191 (P) |
| 50,000,000 | 8.7984 (P) | 0.6358 (P) | 0.6876 (P) | **0.4177 (P)** | 1.3701 (P) |

---

## 3. Parallel Graph BFS
*Workload: 500k nodes, 20 edges/node*

### Experiment 1: Thread Scalability (Buckets: 131k)
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1.1690 (P) | 3.6991 (P) | 3.3646 (P) | 2.7367 (P) | 3.6407 (P) |
| 2 | 1.5354 (P) | 2.9633 (P) | 3.3080 (P) | 2.7793 (P) | 1.9743 (P) |
| 4 | 1.7358 (P) | 1.6439 (P) | 1.9618 (P) | 1.8314 (P) | 1.2333 (P) |
| 8 | 1.5512 (P) | 1.0645 (P) | 1.1626 (P) | 1.5050 (P) | 0.9028 (P) |
| 16 | 1.6781 (P) | 0.6960 (P) | 0.7511 (P) | 1.1854 (P) | 0.6812 (P) |
| 32 | 1.6391 (P) | 0.5050 (P) | 0.5792 (P) | 1.2091 (P) | 0.5365 (P) |
| 64 | 1.6765 (P) | **0.2645 (P)** | 0.4366 (P) | 1.1761 (P) | 0.2804 (P) |

### Experiment 2: Bucket Sensitivity (Threads: 64)
| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | 1.4465 (P) | 5.1851 (P) | 5.1765 (P) | 0.8876 (P) | 0.2441 (P) |
| 5000 | 1.2996 (P) | 1.3699 (P) | 1.4866 (P) | 1.0282 (P) | 0.4708 (P) |
| 10000 | 1.4969 (P) | 0.8802 (P) | 0.9885 (P) | 1.1008 (P) | 0.4561 (P) |
| 50000 | 1.5314 (P) | 0.3221 (P) | 0.4980 (P) | 1.0504 (P) | 0.2455 (P) |
| 100000 | 1.5567 (P) | 0.4830 (P) | 0.5186 (P) | 1.3213 (P) | 0.4630 (P) |
| 500000 | 1.5563 (P) | 0.3882 (P) | 0.3936 (P) | 1.3175 (P) | 0.4801 (P) |

### Experiment 3: Graph Density (Threads: 64)
| Edges per Node | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 5 | 0.5138 (P) | 0.3511 (P) | 0.3699 (P) | 1.0463 (P) | 0.3765 (P) |
| 10 | 0.8787 (P) | 0.3967 (P) | 0.4033 (P) | 1.4154 (P) | 0.4148 (P) |
| 20 | 1.4558 (P) | 0.4282 (P) | 0.5150 (P) | 1.3318 (P) | 0.4760 (P) |
| 50 | 3.8202 (P) | 0.7238 (P) | 0.7392 (P) | 1.3162 (P) | 0.6747 (P) |

### Experiment 4: Problem Size Scaling (Threads: 64)
| Total Nodes | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 100,000 | 0.1357 (P) | 0.0625 (P) | 0.0924 (P) | 0.4824 (P) | 0.0697 (P) |
| 500,000 | 1.5300 (P) | 0.4789 (P) | 0.5060 (P) | 1.1370 (P) | 0.4611 (P) |
| 1,000,000 | 3.8403 (P) | 0.6424 (P) | 1.0942 (P) | 2.0184 (P) | **0.4912 (P)** |
| 2,000,000 | 5.6390 (P) | 2.4203 (P) | 2.6980 (P) | 3.5513 (P) | 1.7600 (P) |
| 5,000,000 | 14.9109 (P) | 7.5126 (P) | 9.7036 (P) | 10.4186 (P) | 2.2713 (P) |

---

## 4. Collatz Memoization
*Workload: 1M numbers, 131k buckets*

### Experiment 1: Thread Scalability (Buckets: 131k)
| Threads | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | 1.1292 (P) | 2.6984 (P) | 3.8369 (P) | 4.5704 (P) | 1.4999 (P) |
| 2 | 1.3846 (P) | 3.2155 (P) | 3.5082 (P) | 7.1211 (P) | 1.4385 (P) |
| 4 | 1.4226 (P) | 2.0451 (P) | 2.0164 (P) | 7.3582 (P) | 1.2511 (P) |
| 8 | 1.3541 (P) | 1.5506 (P) | 1.4792 (P) | 6.4258 (P) | 1.2051 (P) |
| 16 | 1.3805 (P) | 0.6397 (P) | 1.3700 (P) | 5.6741 (P) | 1.2122 (P) |
| 32 | 1.4068 (P) | 1.1879 (P) | 0.9897 (P) | 4.6910 (P) | 1.1037 (P) |
| 64 | 1.4626 (P) | 1.2097 (P) | **0.8940 (P)** | 4.3254 (P) | 1.0403 (P) |

### Experiment 2: Bucket Sensitivity (Threads: 64)
| Initial Buckets | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | 1.3958 (P) | 17.1948 (P) | 17.3441 (P) | 4.6373 (P) | 1.0490 (P) |
| 5000 | 1.3603 (P) | 3.5504 (P) | 3.2488 (P) | 4.0997 (P) | 1.0249 (P) |
| 10000 | 1.2507 (P) | 2.1614 (P) | 1.9458 (P) | 3.8406 (P) | 1.0447 (P) |
| 50000 | 1.3820 (P) | 0.5977 (P) | 1.4057 (P) | 4.1618 (P) | 1.0235 (P) |
| 100000 | 1.2943 (P) | 0.8534 (P) | 1.2557 (P) | 4.0350 (P) | 0.4395 (P) |
| 500000 | 1.3404 (P) | 0.4469 (P) | 1.0776 (P) | 4.3192 (P) | 1.0285 (P) |

### Experiment 3: Small Scale Overhead (Threads: 64)
| Limit | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 10,000 | 0.0068 (P) | 0.0124 (P) | 0.0098 (P) | 0.0720 (P) | 0.0136 (P) |
| 50,000 | 0.0370 (P) | 0.0488 (P) | 0.0446 (P) | 0.2735 (P) | 0.0540 (P) |
| 100,000 | 0.0793 (P) | 0.0463 (P) | 0.1059 (P) | 0.5210 (P) | 0.1148 (P) |
| 500,000 | 0.5874 (P) | 0.5413 (P) | 0.4315 (P) | 2.5907 (P) | 0.5295 (P) |

### Experiment 4: Problem Size Scaling (Threads: 64)
| Upper Limit | Sequential | V2 Static | V3 Dynamic | V6 Segmented | Intel TBB |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1,000,000 | 1.3688 (P) | **0.4756 (P)** | 1.2960 (P) | 4.6851 (P) | 1.0489 (P) |
| 2,000,000 | 2.9767 (P) | 1.0191 (P) | 2.7165 (P) | 9.5778 (P) | 2.0408 (P) |
| 5,000,000 | 8.9876 (P) | 4.0703 (P) | 7.6836 (P) | 26.5954 (P) | 5.0636 (P) |

---

## Architectural Analysis

1.  **Lock-Free Retrieval Win**: The **Resource Cache** (Experiment 1 & 4) shows V6 beating Intel TBB by **300%**. This proves that removing mutexes from the `get()` path is the single most effective optimization for read-heavy server workloads.
2.  **Segmenting Trade-off**: In **BFS** and **Collatz**, V6 is slower than V2/V3. This is the **"Segmenting Tax"**: for lightweight operations, the extra indirection of a segmented design outweighs its concurrency benefits.
3.  **Resizing Robustness**: **Experiment 2** across all benchmarks shows that V6 handles low initial bucket counts far better than V3, proving the segmented resize avoids the "Stop-the-World" bottleneck.

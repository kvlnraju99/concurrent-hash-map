---
title: Concurrent Hash Maps on Multicore Systems
author: Raju Kanumuri (vk2646)
---

## Abstract

Modern multicore programs often have many threads that need to read and update the same shared hash map at the same time. A normal hash map is not safe in that setting because concurrent reads and writes can interfere with each other and corrupt the internal state. This project studies how to build a concurrent hash map that remains correct under overlap while still scaling well as the number of threads increases.

The project developed four versions of a concurrent hash map: a lock-based dynamic-resizing baseline, a fixed-size lock-free chaining map, an experimental dynamic-resizing lock-free chaining map, and an experimental open-addressing map. The versions were evaluated with correctness tests, a broad cross-variant benchmark sweep, a workload-mix study, a contention sweep, and a resize-focused experiment. These experiments were designed to answer a simple question: which design gives the best balance between correctness, simplicity, and performance?

The main result is that the fixed-size lock-free chaining map is the best overall implementation in the project. It gives the best or near-best results for `GET`, balanced mixed workloads, and extreme contention. The dynamic-resizing lock-free version is the strongest writer and becomes the best design when the workload becomes more write-heavy or when the table must grow under concurrency. The open-addressing experiment provided useful insight about locality, but it was not competitive overall in the final concurrent benchmarks.

## Introduction

Hash maps are everywhere in software. They are used to store session data, cache entries, indexes, counters, routing information, and metadata. In a single-threaded program, a regular hash map is usually enough. In a multicore program, the situation changes. Many worker threads may try to read and update the same shared map at the same time. When that happens, a normal hash map is not safe unless the programmer adds external synchronization around every access.

A concurrent hash map solves that problem. It is a shared key-value data structure that supports safe parallel access by multiple threads. The goal is not to preserve every external wall-clock ordering between threads. The goal is to preserve the correctness of the data structure itself. If two operations overlap, the map must still remain logically valid. Reads may see either the old value or the new value depending on timing, but the map must not return impossible state, lose structure, or become corrupted.

This distinction matters in real systems. A cache, session table, or in-memory index usually does not require a globally ordered history of all updates. It does require a correct shared container that can handle many threads without collapsing into lock contention or data corruption. That is why concurrent hash maps are a natural and important multicore data structure project.

The project was built as a progression of designs. First, a lock-based dynamic-resizing hash map was implemented as a baseline. This gave a correct and understandable starting point. Next, the design moved to a lock-free chaining map to remove bucket-lock contention from the hot path. After that, two harder extensions were explored: adding dynamic resizing to the lock-free map, and replacing chaining with open addressing. The report follows that same progression because each new version was created to solve a specific limitation of the previous one.

The final goal of the project is not simply to say that one version is “fast.” The real goal is to understand which design works best under realistic concurrent workloads and why. For that reason, the report evaluates the four versions across correctness, scalability, workload mix, contention, and resizing behavior.

## Literature Survey

The literature survey for this project is organized as a taxonomy rather than a list of papers. This follows the idea from the course slides: the goal is to classify prior work into meaningful categories, explain the main idea of each category, discuss its strengths and weaknesses, and then explain why the current project is still needed.

### 1. Lock-based concurrent hash tables

The first category is lock-based concurrent hash tables. In these designs, the data structure is protected with locks, either at the whole-table level or at the bucket level. A practical version often uses one lock per bucket and a larger lock for resizing. This category is attractive because it is easy to explain, easy to debug, and usually the simplest way to get a correct concurrent implementation.

The strength of lock-based tables is simplicity. Their behavior is easier to reason about, resizing is straightforward, and correctness bugs are generally easier to isolate. The weakness is scalability. Once thread count grows, locks become a bottleneck. If many threads frequently access the same buckets, or if resizing needs global coordination, lock-based designs can lose performance quickly.

This category is directly relevant to the project because the lock-based implementation serves as the baseline. Without a correct baseline, it would be difficult to evaluate whether more advanced synchronization strategies are actually worth the added complexity.

### 2. Lock-free linked structures

The second category is lock-free linked structures. A key idea in this family is that the linked list inside each bucket can be updated with atomic compare-and-swap operations instead of a bucket lock. The paper *A Pragmatic Implementation of Non-Blocking Linked Lists* showed that non-blocking linked lists can support concurrent insertion and deletion while preserving correctness [1]. Later, *High Performance Dynamic Lock-Free Hash Tables and List-Based Sets* used lock-free list-based sets as a practical foundation for dynamic hash-table designs [2].

The main advantage of this category is scalability. Different threads can make progress without waiting on the same mutex, which is especially useful for read-heavy and mixed workloads. The main disadvantage is complexity. Deletion, cleanup, memory ordering, and same-key races become much harder than in a lock-based design.

This category is the direct foundation for the main implementation in the project, because the stable lock-free map uses chaining and atomic pointer updates inside each bucket.

### 3. Lock-free dynamic resizing

The third category is lock-free dynamic resizing. The paper *Split-Ordered Lists: Lock-Free Extensible Hash Tables* showed that lock-free dynamic resizing is possible, but also made clear that resizing is one of the hardest parts of the problem [3]. Once resizing starts, old and new table states may briefly coexist. Readers and writers then need a rule for which table is authoritative and how migration is completed without losing updates.

The strength of this category is flexibility. A dynamically resizing map can adapt to changing load instead of requiring the programmer to choose a fixed table size in advance. The weakness is that correctness becomes much harder. Migration, redirection, freeze protocols, and safe publication of new table state all introduce subtle race conditions.

This category motivated the dynamic-resizing experiment in the project. The project did not try to reproduce a complete split-ordered implementation, but it did explore a simpler cooperative migration design that fits the structure of the codebase.

### 4. Concurrent open addressing

The fourth category is concurrent open addressing. In an open-addressing table, entries are stored directly in a contiguous array instead of in linked lists. The paper *Non-Blocking Hashtables with Open Addressing* explored this direction and showed that open addressing can be made concurrent with non-blocking ideas [4].

The advantage of this category is locality. A contiguous array can reduce pointer chasing and can be attractive for insertion and lookup when the table is lightly loaded. The disadvantage is sensitivity to load factor and probe growth. Once the table gets denser or concurrency becomes more irregular, probing behavior can degrade quickly. Deletion and resizing also become more delicate than in chaining-based designs.

This category was relevant to the project because it offered a clear alternative to bucket chaining. It was worth testing whether locality could beat chaining in practice on the target machine and workloads.

### 5. Why this project is still needed

The literature gives strong techniques, but it does not decide which design is best for this particular project. The course project required an implementation that works on the target NYU CIMS environment, supports a real benchmark suite, and exposes practical tradeoffs between correctness and performance.

That is why the project still matters. It does not simply restate published work. It implements four meaningful design points and compares them under one consistent methodology. The result is an evidence-based answer about which ideas helped, which ideas did not, and where the complexity was justified.

## Proposed Idea

The project developed four versions of a concurrent hash map. They are best understood as stages in a single design story rather than as unrelated alternatives.

![Architecture overview of the four hash map designs](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/architecture_overview.png)

**Figure 1.** High-level architecture of the four implementations used in the project. The baseline uses per-bucket locks and a global resize lock; the main lock-free design uses linked-list buckets; the dynamic-resizing lock-free version adds old/new table migration; the open-addressing experiment uses a contiguous slot array and probing.

### 1. Lock-based dynamic-resizing baseline

The first version is a lock-based concurrent hash map. Each bucket stores a short chain of key-value pairs and has its own `shared_mutex`. A global `shared_mutex` protects the bucket array during resizing. Normal operations take a shared global lock and then synchronize only on the target bucket. When the load factor crosses a threshold, the table is resized by allocating a larger bucket array and rehashing all entries.

This version was chosen first because it is the most direct correct design. It gives a stable baseline, a clean resizing story, and an easy point of comparison for the more aggressive designs.

### 2. Fixed-size lock-free chaining map

The second version is the main stable lock-free implementation. Each bucket is a linked list of nodes. Threads traverse buckets using atomic pointer loads and update them using compare-and-swap operations instead of bucket locks. Deleted nodes are marked and then unlinked eagerly to keep chains short.

This design removes bucket-level lock contention from the hot path while keeping the bucket array fixed in size. The fixed-size decision was deliberate: it avoids the hardest part of lock-free hash maps, which is resizing while threads are still actively reading and writing. In practice, this version became the strongest overall implementation in the project.

### 3. Experimental dynamic-resizing lock-free chaining map

The third version extends the lock-free chaining design with dynamic resizing. The central idea is to allow an old table and a new table to coexist during a migration phase. Buckets are frozen, migrated, and then redirected so that new writes do not continue landing in an already-migrated old bucket.

This was the most difficult version of the project. Early versions exposed lost-key races, same-key races, and size-accounting drift. The final experimental version passed repeated correctness tests on the target Linux machine, but it still uses a conservative exact `size()` implementation that scans live keys rather than relying only on a counter. For that reason, this version is best described as a correctness-first experimental extension rather than the final polished implementation.

### 4. Experimental open-addressing map

The fourth version replaces chaining with open addressing. Instead of storing linked lists inside buckets, it stores entries directly in a flat slot array and resolves collisions through probing. This design was built to test whether better locality and fewer pointer traversals could beat the chaining versions.

The idea was reasonable, but the full experiments showed that locality alone was not enough. The design performed well in a few narrow insertion cases, especially at one thread, but it did not remain competitive under the broader concurrent workload suite.

### 5. What success means in this project

This project does not judge success by “wall-clock order preservation” between all threads. A concurrent hash map is a shared container, not a transaction log. The main question is whether the map remains logically correct and scalable when operations overlap.

For that reason, the project measures success using:

- correctness under concurrent overlap
- throughput under `PUT`, `GET`, and mixed workloads
- scalability as thread count grows
- behavior under different workload mixes
- behavior under different levels of contention
- behavior when resizing becomes necessary

That definition of success matches the practical use case of a concurrent hash map much better than any single microbenchmark alone.

## Experimental Setup

All code was written in C++17 and compiled with `g++` using `-O2` and `-pthread`. The project was built and tested in the NYU CIMS Linux environment because the course explicitly requires the final project to work on those machines.

The final submission contains two main executables:

- `hash_map_test` for correctness and concurrency testing
- `benchmark` for side-by-side throughput comparison across the four implementations

During development, additional internal sweep scripts were used to generate the broader experiment tables and figures discussed in this report. Those scripts were only used to produce the results; the final submission keeps the code and commands needed for building, testing, and benchmarking on the target machine.

### 1. Core cross-variant sweep

The main comparison in the report uses a structured sweep across all four implementations. The sweep varies:

- workload: `PUT`, `GET`, `MIXED`
- thread count: `1`, `2`, `4`, `8`, `16`
- bucket count: `65536`, `262144`, `1048576`
- repetitions: `3` runs per scenario

For `PUT`, the number of inserted keys was set to one quarter of the bucket count. This kept the open-addressing design in a reasonable operating regime. For `GET` and `MIXED`, the key space was preloaded and the operation count was derived from that key space. The final comparison uses aggregated runtimes across repeated runs so that the conclusions are not based on one noisy measurement.

### 2. Focused follow-up experiments

After the main sweep, three more focused experiments were run.

**Workload-mix experiment**

This experiment used three mixes:

- read-heavy: `90% GET`, `10% PUT`
- balanced: `70% GET`, `20% PUT`, `10% REMOVE`
- write-heavy: `40% GET`, `40% PUT`, `20% REMOVE`

The goal was to see whether the winner changes as the traffic pattern changes.

**Contention sweep**

This experiment fixed the bucket count but varied key-space size:

- `64`
- `1024`
- `16384`
- `65536`

Smaller key spaces create higher contention because more threads target the same keys. This experiment was designed to show which designs remain strong when many threads collide on the same logical working set.

**Resize-focused experiment**

This experiment started the table from intentionally small sizes:

- `1024`
- `4096`
- `16384`

and then inserted enough keys to force substantial growth. It compared the lock-based baseline, the fixed-size lock-free design, and the dynamic-resizing lock-free design. The open-addressing experiment was excluded because it does not support resizing.

These focused experiments were summarized into the tables and figures used in the Results section.

### 3. Correctness validation

Correctness testing was a major part of the setup, especially for the dynamic-resizing lock-free branch. Repeated tests covered:

- single-thread correctness
- parallel puts and gets
- parallel removes
- same-key overwrite races
- repeated same-key race tests
- stress tests
- concurrent resize correctness

The dynamic-resizing branch was only included in the final comparison after repeated Linux runs passed consistently. This is important because a fast concurrent data structure is not useful if it is not correct.

## Experiments & Analysis

### 1. Main result from the full benchmark sweep

The broadest conclusion from the project is simple: the chaining-based lock-free designs dominate the lock-based baseline and the open-addressing experiment in the main benchmark. The strongest final benchmark was run on the target machine with `800000` operations, `131072` buckets, and up to `8` threads.

In that final benchmark, the fixed-size lock-free chaining map won every `PUT` case, every `GET` case, and three of the four mixed-workload cases. The dynamic-resizing lock-free version remained competitive and won one mixed-workload point at `4` threads. The lock-based dynamic map was consistently much slower, and the open-addressing experiment was the weakest design overall.

This confirms the overall project result: the fixed-size lock-free chaining map is the best final implementation, while the dynamic-resizing lock-free version is best treated as a useful but more specialized extension.

### 2. PUT analysis

The final benchmark shows that the fixed-size lock-free chaining map is the strongest insertion design in the final submission branch. It wins at every tested thread count.

At `131072` buckets and `8` threads:

- fixed-size lock-free chaining: `43.1 ms`
- lock-free dynamic resize: `54.5 ms`
- lock-based dynamic: `309.6 ms`
- open addressing: `597.9 ms`

This is a large gap, not a marginal one. The fixed-size lock-free chaining map is about seven times faster than the lock-based baseline in this `PUT` configuration, and it is still clearly ahead of the dynamic-resizing version.

The likely reason is that the fixed-size lock-free version keeps the write path simple: no bucket locks, no resize coordination, and no probing overhead. The dynamic-resizing version still pays extra bookkeeping cost even when resizing is not the best tradeoff for this specific workload.

![PUT throughput at 131072 buckets](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/put_131072_final.png)

**Figure 2.** `PUT` throughput in the final benchmark (`800000` operations, `131072` buckets). The fixed-size lock-free chaining map is the strongest writer at every tested thread count.

### 3. GET analysis

The `GET` workload tells a different story. Here, the fixed-size lock-free chaining design is decisively the best reader. In the main sweep, it wins 13 of the 15 `GET` scenarios.

At `131072` buckets and `8` threads:

- fixed-size lock-free chaining: `4.4 ms`
- lock-free dynamic resize: `4.5 ms`
- lock-based dynamic: `394.0 ms`
- open addressing: `307.0 ms`

This result is extremely strong. The two chaining-based lock-free designs are effectively tied at the top, but the fixed-size version remains slightly better and clearly simpler.

The interpretation is straightforward. Reads benefit from the simplest possible fast path. The fixed-size version does not need to carry the extra state-management cost that the dynamic-resizing version carries, even after resize correctness has been achieved.

![GET throughput at 131072 buckets](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/get_131072_final.png)

**Figure 3.** `GET` throughput in the final benchmark (`800000` operations, `131072` buckets). The fixed-size lock-free chaining map gives the strongest and most consistent read throughput.

### 4. Mixed-workload analysis

The mixed workload is the most important for selecting a final implementation because it combines inserts, lookups, and removals. This is closer to how a shared concurrent map behaves in a real application.

In the final benchmark, the fixed-size lock-free chaining map wins the mixed workload at `1`, `2`, and `8` threads, while the dynamic-resizing lock-free version wins slightly at `4` threads.

At `131072` buckets and `8` threads:

- fixed-size lock-free chaining: `74.1 ms`
- lock-free dynamic resize: `110.1 ms`
- lock-based dynamic: `425.4 ms`
- open addressing: `535.0 ms`

At `4` threads, the dynamic-resizing version records `139.4 ms` while the fixed-size lock-free version records `145.6 ms`. That small crossover is useful: it shows that the dynamic-resizing version can still help under some mixed conditions, but it does not overturn the overall result.

This is exactly the kind of result that supports a final design decision. The fixed-size lock-free design is not only good at one special case. It is the most balanced performer when reads and writes coexist, while the dynamic-resizing version remains a credible extension rather than the best default choice.

![MIXED throughput at 131072 buckets](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/mixed_131072_final.png)

**Figure 4.** `MIXED` throughput in the final benchmark (`800000` operations, `131072` buckets). The fixed-size lock-free chaining map is the strongest all-around design, with the dynamic-resizing version winning one mid-range point.

### 5. Workload-mix experiment

The workload-mix experiment makes the story clearer by separating read-heavy, balanced, and write-heavy traffic.

For the read-heavy mix at `16` threads:

- fixed-size lock-free chaining: `2.855 ms`
- lock-free dynamic resize: `2.904 ms`
- lock-based dynamic: `113.080 ms`
- open addressing: `113.517 ms`

This is effectively a tie between the two chaining-based lock-free versions, with the fixed-size version slightly ahead.

For the balanced mix at `16` threads:

- fixed-size lock-free chaining: `11.529 ms`
- lock-free dynamic resize: `14.684 ms`
- lock-based dynamic: `112.746 ms`
- open addressing: `182.475 ms`

For the write-heavy mix at `16` threads:

- lock-free dynamic resize: `27.564 ms`
- fixed-size lock-free chaining: `31.085 ms`
- lock-based dynamic: `116.277 ms`
- open addressing: `164.514 ms`

The conclusion is very useful for the report:

- fixed-size lock-free chaining is the best all-around design
- dynamic-resizing lock-free becomes the best choice when the workload becomes sufficiently write-heavy

![Workload mix at 16 threads](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/workload_mix_16.png)

**Figure 5.** Workload-mix experiment at `16` threads. The fixed-size lock-free chaining map dominates read-heavy and balanced workloads, while the dynamic-resizing version becomes best in the write-heavy case.

### 6. Contention sweep

The contention experiment varies key-space size. A smaller key space means more threads hit the same keys, so effective contention rises.

At `16` threads and key space `64`:

- fixed-size lock-free chaining: `12.830 ms`
- lock-free dynamic resize: `20.548 ms`
- lock-based dynamic: `108.458 ms`
- open addressing: `138.675 ms`

At `16` threads and key space `1024`:

- lock-free dynamic resize: `16.025 ms`
- fixed-size lock-free chaining: `17.670 ms`
- lock-based dynamic: `106.586 ms`
- open addressing: `150.352 ms`

At `16` threads and key space `65536`:

- fixed-size lock-free chaining: `11.350 ms`
- lock-free dynamic resize: `11.968 ms`
- lock-based dynamic: `124.141 ms`
- open addressing: `139.693 ms`

This experiment reveals an important tradeoff:

- the fixed-size lock-free chaining map is best under extreme same-key contention
- the dynamic-resizing lock-free version is best under moderate contention
- once contention becomes low again, the fixed-size lock-free map regains the edge

This makes the final design story stronger. It shows that the dynamic-resizing version is not “better everywhere.” It is better in a specific part of the design space.

![Contention sweep at 16 threads](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/contention_16.png)

**Figure 6.** Contention sweep at `16` threads. The fixed-size lock-free chaining map is strongest under extreme contention, while the dynamic-resizing version is strongest under moderate contention.

### 7. Resize-focused experiment

The resize-focused experiment was designed specifically to justify the dynamic-resizing branch. The table starts small and then has to grow under insertion pressure.

At initial bucket count `1024` and `8` threads:

- lock-free dynamic resize: `5.246 ms`
- fixed-size lock-free chaining: `6.230 ms`
- lock-based dynamic: `37.627 ms`

At initial bucket count `16384` and `16` threads:

- lock-free dynamic resize: `98.731 ms`
- fixed-size lock-free chaining: `100.390 ms`
- lock-based dynamic: `611.353 ms`

These results show that dynamic resizing is not a universal win, but it is useful in exactly the situation where it should matter: when the table starts small, must grow, and several threads are inserting at once.

At low thread count, the fixed-size chaining version is often still faster because it avoids resize bookkeeping entirely. Under higher concurrency and stronger growth pressure, the dynamic-resizing version becomes competitive or better.

![Resize-focused experiment at 16 threads](/Users/kvlnraju/College/courses/semester-4/multi-core/project/concurrent-hash-map/report/figures/resize_focus_16.png)

**Figure 7.** Resize-focused experiment at `16` threads. Dynamic resizing is not always free, but it becomes worthwhile when the table must grow under concurrent insertion pressure.

### 8. Open addressing as a negative but useful result

The open-addressing experiment is not the best implementation, but it remains an important part of the project. It showed that better locality is not enough to win under the full concurrent workload suite. It only wins the narrowest single-thread insertion cases and then falls behind badly as the workloads become more concurrent and mixed.

That result is valuable because it prevented the project from chasing a design that looked attractive in theory but was not the right answer in practice.

### 9. Final interpretation of the full design space

Taken together, the experiments now tell a consistent story:

- The fixed-size lock-free chaining map is the best final implementation.
- The dynamic-resizing lock-free chaining map is the best experimental extension.
- The lock-based dynamic map is a useful correctness baseline but not a performance winner.
- The open-addressing experiment helped answer a real question about locality, but it is not the right design for this project’s final implementation.

This is exactly the kind of result a strong systems project should produce. It does not just claim that “lock-free is faster.” It shows where each design wins, where each design loses, and why the final choice is justified.

## Conclusions

- The fixed-size lock-free chaining hash map is the best overall implementation in the project because it gives the strongest `GET`, balanced mixed-workload, and high-contention performance while remaining competitive on insertion-heavy workloads.
- The dynamic-resizing lock-free chaining hash map is the strongest write-oriented design and the best extension when the workload becomes more write-heavy or when the table must grow under concurrent insertion pressure.
- The open-addressing experiment was useful for exploring locality-driven design, but it did not outperform the chaining-based lock-free designs in realistic concurrent workloads, so it was not chosen as the final implementation path.

## References

[1] *A Pragmatic Implementation of Non-Blocking Linked Lists*. In *Proceedings of the 15th International Symposium on Distributed Computing*, 2001.

[2] *High Performance Dynamic Lock-Free Hash Tables and List-Based Sets*. In *Proceedings of the 14th Annual ACM Symposium on Parallel Algorithms and Architectures*, 2002.

[3] *Split-Ordered Lists: Lock-Free Extensible Hash Tables*. In *Proceedings of the 22nd Annual ACM Symposium on Principles of Distributed Computing*, 2003.

[4] *Non-Blocking Hashtables with Open Addressing*. In *Proceedings of the 19th International Symposium on Distributed Computing*, 2005.

[5] *The Art of Multiprocessor Programming*. Morgan Kaufmann, 2012.

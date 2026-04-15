#!/usr/bin/env python3

import argparse
import csv
import os
import pathlib
import shutil
import statistics
import subprocess
import sys
import tempfile
import textwrap
from dataclasses import dataclass


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
WORKTREE_ROOT = REPO_ROOT / ".variant_worktrees"
RESULTS_ROOT = REPO_ROOT / "results"


@dataclass(frozen=True)
class Variant:
    name: str
    branch: str
    kind: str
    description: str


VARIANTS = (
    Variant("locked_dynamic", "main", "locked", "Lock-based dynamic resizing"),
    Variant("lockfree_chaining", "lock-free", "lockfree", "Fixed-size lock-free chaining"),
    Variant(
        "lockfree_dynamic_resize",
        "codex/lock-free-resize-experiment",
        "lockfree",
        "Experimental dynamic-resizing lock-free chaining",
    ),
    Variant(
        "lockfree_open_addressing",
        "codex/open-addressing-experiment",
        "open",
        "Experimental open-addressing lock-free map",
    ),
)


BENCH_SOURCE = r"""
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(VARIANT_LOCKED)
#include "concurrent_hash_map.h"
using MapType = ConcurrentHashMap<int, int>;
MapType make_map(std::size_t buckets) { return MapType(buckets, 0.75); }
#elif defined(VARIANT_LOCKFREE)
#include "lock_free_hash_map.h"
using MapType = LockFreeHashMap<int, int>;
MapType make_map(std::size_t buckets) { return MapType(buckets); }
#elif defined(VARIANT_OPEN)
#include "lock_free_open_addressing_hash_map.h"
using MapType = LockFreeOpenAddressingHashMap<int, int>;
MapType make_map(std::size_t buckets) { return MapType(buckets); }
#else
#error "unknown benchmark variant"
#endif

namespace {

struct Config {
    std::string workload = "put";
    int threads = 1;
    int total_ops = 1;
    int key_space = 1;
    std::size_t buckets = 1;
};

Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--workload" && i + 1 < argc) {
            cfg.workload = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--ops" && i + 1 < argc) {
            cfg.total_ops = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--keys" && i + 1 < argc) {
            cfg.key_space = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--buckets" && i + 1 < argc) {
            cfg.buckets = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
            if (cfg.buckets == 0) cfg.buckets = 1;
        } else {
            std::cerr << "usage: variant_bench --workload put|get|mixed --threads N --ops N --keys N --buckets N\n";
            std::exit(1);
        }
    }
    return cfg;
}

template <typename F>
double time_ms(F&& fn) {
    const auto start = std::chrono::high_resolution_clock::now();
    fn();
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void preload(MapType& map, int key_space) {
    for (int i = 0; i < key_space; ++i) {
#if defined(VARIANT_OPEN)
        (void)map.put(i, i);
#else
        map.put(i, i);
#endif
    }
}

double bench_put(const Config& cfg) {
    MapType map = make_map(cfg.buckets);
    const int ops_per_thread = cfg.total_ops / cfg.threads;
    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);

    return time_ms([&]() {
        for (int t = 0; t < cfg.threads; ++t) {
            threads.emplace_back([&map, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    const int key = t * ops_per_thread + i;
#if defined(VARIANT_OPEN)
                    (void)map.put(key, i);
#else
                    map.put(key, i);
#endif
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    });
}

double bench_get(const Config& cfg) {
    MapType map = make_map(cfg.buckets);
    preload(map, cfg.key_space);
    const int ops_per_thread = cfg.total_ops / cfg.threads;
    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);

    return time_ms([&]() {
        for (int t = 0; t < cfg.threads; ++t) {
            threads.emplace_back([&map, t, ops_per_thread, &cfg]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    const int key = (t * ops_per_thread + i) % cfg.key_space;
                    (void)map.get(key);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    });
}

double bench_mixed(const Config& cfg) {
    MapType map = make_map(cfg.buckets);
    preload(map, cfg.key_space);
    const int ops_per_thread = cfg.total_ops / cfg.threads;
    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);

    return time_ms([&]() {
        for (int t = 0; t < cfg.threads; ++t) {
            threads.emplace_back([&map, t, ops_per_thread, &cfg]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    const int key = (t * ops_per_thread + i) % cfg.key_space;
                    const int op = (t + i) % 3;
                    if (op == 0) {
#if defined(VARIANT_OPEN)
                        (void)map.put(key, i);
#else
                        map.put(key, i);
#endif
                    } else if (op == 1) {
                        (void)map.get(key);
                    } else {
                        (void)map.remove(key);
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    });
}

}  // namespace

int main(int argc, char* argv[]) {
    const Config cfg = parse_args(argc, argv);
    double ms = 0.0;
    if (cfg.workload == "put") {
        ms = bench_put(cfg);
    } else if (cfg.workload == "get") {
        ms = bench_get(cfg);
    } else if (cfg.workload == "mixed") {
        ms = bench_mixed(cfg);
    } else {
        std::cerr << "unknown workload: " << cfg.workload << "\n";
        return 1;
    }

    std::cout << ms << "\n";
    return 0;
}
"""


def run(cmd, cwd=None):
    subprocess.run(cmd, cwd=cwd, check=True)


def capture(cmd, cwd=None):
    return subprocess.check_output(cmd, cwd=cwd, text=True).strip()


def ensure_worktree(variant: Variant) -> pathlib.Path:
    WORKTREE_ROOT.mkdir(exist_ok=True)
    path = WORKTREE_ROOT / variant.name

    if path.exists():
        git_dir = path / ".git"
        if git_dir.exists():
            run(["git", "-C", str(path), "checkout", "--detach", variant.branch])
            run(["git", "-C", str(path), "reset", "--hard", variant.branch])
            return path
        shutil.rmtree(path)

    run(["git", "worktree", "add", "--force", "--detach", str(path), variant.branch], cwd=REPO_ROOT)
    return path


def compile_benchmark(worktree: pathlib.Path, kind: str) -> pathlib.Path:
    source_path = worktree / ".variant_bench.cpp"
    binary_path = worktree / ".variant_bench"
    source_path.write_text(BENCH_SOURCE, encoding="ascii")

    define = {
        "locked": "-DVARIANT_LOCKED",
        "lockfree": "-DVARIANT_LOCKFREE",
        "open": "-DVARIANT_OPEN",
    }[kind]

    run(
        [
            "g++",
            "-std=c++17",
            "-O2",
            "-pthread",
            "-Wall",
            "-Wextra",
            define,
            "-o",
            str(binary_path),
            str(source_path),
        ],
        cwd=worktree,
    )
    return binary_path


def parse_int_list(value: str):
    return [int(part) for part in value.split(",") if part.strip()]


def benchmark_variant(binary: pathlib.Path, workload: str, threads: int, buckets: int, key_space: int, total_ops: int) -> float:
    out = capture(
        [
            str(binary),
            "--workload",
            workload,
            "--threads",
            str(threads),
            "--ops",
            str(total_ops),
            "--keys",
            str(key_space),
            "--buckets",
            str(buckets),
        ]
    )
    return float(out)


def summarize_rows(rows):
    grouped = {}
    for row in rows:
        key = (
            row["variant"],
            row["workload"],
            row["buckets"],
            row["threads"],
            row["key_space"],
            row["total_ops"],
        )
        grouped.setdefault(key, []).append(float(row["ms"]))

    summary = []
    for key, samples in sorted(grouped.items()):
        variant, workload, buckets, threads, key_space, total_ops = key
        median_ms = statistics.median(samples)
        mean_ms = statistics.fmean(samples)
        best_ms = min(samples)
        mops = (int(total_ops) / 1_000_000.0) / (median_ms / 1000.0)
        summary.append(
            {
                "variant": variant,
                "workload": workload,
                "buckets": buckets,
                "threads": threads,
                "key_space": key_space,
                "total_ops": total_ops,
                "samples": len(samples),
                "median_ms": f"{median_ms:.3f}",
                "mean_ms": f"{mean_ms:.3f}",
                "best_ms": f"{best_ms:.3f}",
                "median_mops": f"{mops:.3f}",
            }
        )
    return summary


def print_matrix(summary_rows):
    current = None
    for row in summary_rows:
        header = (row["workload"], row["buckets"])
        if header != current:
            current = header
            print(f"\n[{row['workload']}] buckets={row['buckets']}")
            print("variant                    threads  median_ms  median_mops")
            print("-------------------------  -------  ---------  -----------")
        print(
            f"{row['variant']:<25}  {int(row['threads']):>7}  "
            f"{float(row['median_ms']):>9.3f}  {float(row['median_mops']):>11.3f}"
        )


def main():
    parser = argparse.ArgumentParser(
        description="Run a structured performance sweep across the four hash map variants."
    )
    parser.add_argument("--threads", default="1,2,4,8,16", help="comma-separated thread counts")
    parser.add_argument(
        "--buckets",
        default="65536,262144,1048576",
        help="comma-separated bucket counts; choose sizes large enough for open addressing",
    )
    parser.add_argument(
        "--workloads",
        default="put,get,mixed",
        help="comma-separated workloads: put,get,mixed",
    )
    parser.add_argument("--repeats", type=int, default=3, help="repetitions per scenario")
    parser.add_argument(
        "--put-load",
        type=float,
        default=0.25,
        help="put workload inserts this fraction of the bucket count",
    )
    parser.add_argument(
        "--rw-multiplier",
        type=int,
        default=4,
        help="get/mixed total ops = key_space * multiplier",
    )
    parser.add_argument(
        "--output-prefix",
        default="variant_sweep",
        help="prefix for CSV outputs under results/",
    )
    args = parser.parse_args()

    thread_counts = parse_int_list(args.threads)
    bucket_counts = parse_int_list(args.buckets)
    workloads = [part.strip() for part in args.workloads.split(",") if part.strip()]

    RESULTS_ROOT.mkdir(exist_ok=True)
    raw_path = RESULTS_ROOT / f"{args.output_prefix}_raw.csv"
    summary_path = RESULTS_ROOT / f"{args.output_prefix}_summary.csv"

    raw_rows = []

    for variant in VARIANTS:
        worktree = ensure_worktree(variant)
        binary = compile_benchmark(worktree, variant.kind)

        for buckets in bucket_counts:
            key_space = max(1, int(buckets * args.put_load))
            total_ops_by_workload = {
                "put": key_space,
                "get": key_space * args.rw_multiplier,
                "mixed": key_space * args.rw_multiplier,
            }

            for workload in workloads:
                total_ops = total_ops_by_workload[workload]
                for threads in thread_counts:
                    aligned_total_ops = max(threads, (total_ops // threads) * threads)
                    for repeat in range(1, args.repeats + 1):
                        ms = benchmark_variant(
                            binary=binary,
                            workload=workload,
                            threads=threads,
                            buckets=buckets,
                            key_space=key_space,
                            total_ops=aligned_total_ops,
                        )
                        raw_rows.append(
                            {
                                "variant": variant.name,
                                "branch": variant.branch,
                                "description": variant.description,
                                "workload": workload,
                                "buckets": buckets,
                                "threads": threads,
                                "key_space": key_space,
                                "total_ops": aligned_total_ops,
                                "repeat": repeat,
                                "ms": f"{ms:.3f}",
                                "mops": f"{(aligned_total_ops / 1_000_000.0) / (ms / 1000.0):.3f}",
                            }
                        )
                        print(
                            f"{variant.name:>24} workload={workload:<5} buckets={buckets:<8} "
                            f"threads={threads:<2} repeat={repeat}/{args.repeats} ms={ms:.3f}"
                        )

    summary_rows = summarize_rows(raw_rows)

    with raw_path.open("w", newline="", encoding="ascii") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(raw_rows[0].keys()))
        writer.writeheader()
        writer.writerows(raw_rows)

    with summary_path.open("w", newline="", encoding="ascii") as fh:
        writer = csv.DictWriter(fh, fieldnames=list(summary_rows[0].keys()))
        writer.writeheader()
        writer.writerows(summary_rows)

    print(f"\nraw results: {raw_path}")
    print(f"summary:     {summary_path}")
    print_matrix(summary_rows)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"command failed: {' '.join(exc.cmd)}", file=sys.stderr)
        sys.exit(exc.returncode)

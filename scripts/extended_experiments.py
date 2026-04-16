#!/usr/bin/env python3

import argparse
import csv
import pathlib
import shutil
import statistics
import subprocess
import sys
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


ALL_VARIANTS = (
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

RESIZE_VARIANTS = tuple(v for v in ALL_VARIANTS if v.kind != "open")


GENERAL_BENCH_SOURCE = r"""
#include <chrono>
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
    int threads = 1;
    int total_ops = 1;
    int key_space = 1;
    std::size_t buckets = 1;
    int get_pct = 70;
    int put_pct = 20;
    int remove_pct = 10;
};

Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--ops" && i + 1 < argc) {
            cfg.total_ops = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--keys" && i + 1 < argc) {
            cfg.key_space = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--buckets" && i + 1 < argc) {
            cfg.buckets = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
            if (cfg.buckets == 0) cfg.buckets = 1;
        } else if (arg == "--get-pct" && i + 1 < argc) {
            cfg.get_pct = std::atoi(argv[++i]);
        } else if (arg == "--put-pct" && i + 1 < argc) {
            cfg.put_pct = std::atoi(argv[++i]);
        } else if (arg == "--remove-pct" && i + 1 < argc) {
            cfg.remove_pct = std::atoi(argv[++i]);
        } else {
            std::cerr << "usage: general_bench --threads N --ops N --keys N --buckets N "
                         "--get-pct N --put-pct N --remove-pct N\n";
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

double bench(const Config& cfg) {
    MapType map = make_map(cfg.buckets);
    preload(map, cfg.key_space);
    const int ops_per_thread = cfg.total_ops / cfg.threads;
    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);

    return time_ms([&]() {
        for (int t = 0; t < cfg.threads; ++t) {
            threads.emplace_back([&map, &cfg, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    const int key = (t * ops_per_thread + i) % cfg.key_space;
                    const int phase = (t * 17 + i) % 100;
                    if (phase < cfg.get_pct) {
                        (void)map.get(key);
                    } else if (phase < cfg.get_pct + cfg.put_pct) {
#if defined(VARIANT_OPEN)
                        (void)map.put(key, i);
#else
                        map.put(key, i);
#endif
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
    std::cout << bench(cfg) << "\n";
    return 0;
}
"""


RESIZE_BENCH_SOURCE = r"""
#include <chrono>
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
#else
#error "resize benchmark only supports locked/lockfree variants"
#endif

namespace {

struct Config {
    int threads = 1;
    int total_ops = 1;
    std::size_t buckets = 1;
};

Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--ops" && i + 1 < argc) {
            cfg.total_ops = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--buckets" && i + 1 < argc) {
            cfg.buckets = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
            if (cfg.buckets == 0) cfg.buckets = 1;
        } else {
            std::cerr << "usage: resize_bench --threads N --ops N --buckets N\n";
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

}  // namespace

int main(int argc, char* argv[]) {
    const Config cfg = parse_args(argc, argv);
    MapType map = make_map(cfg.buckets);
    const int ops_per_thread = cfg.total_ops / cfg.threads;
    std::vector<std::thread> threads;
    threads.reserve(cfg.threads);

    const double ms = time_ms([&]() {
        for (int t = 0; t < cfg.threads; ++t) {
            threads.emplace_back([&map, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    const int key = t * ops_per_thread + i;
#if defined(VARIANT_LOCKFREE)
                    map.put(key, i);
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

    std::cout << ms << "," << map.get_bucket_count() << "," << map.size() << "\n";
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
        if (path / ".git").exists():
            run(["git", "-C", str(path), "checkout", "--detach", variant.branch])
            run(["git", "-C", str(path), "reset", "--hard", variant.branch])
            return path
        shutil.rmtree(path)

    run(["git", "worktree", "add", "--force", "--detach", str(path), variant.branch], cwd=REPO_ROOT)
    return path


def compile_benchmark(worktree: pathlib.Path, variant: Variant, source_text: str, stem: str) -> pathlib.Path:
    source_path = worktree / f".{stem}.cpp"
    binary_path = worktree / f".{stem}"
    source_path.write_text(source_text, encoding="ascii")

    define = {
        "locked": "-DVARIANT_LOCKED",
        "lockfree": "-DVARIANT_LOCKFREE",
        "open": "-DVARIANT_OPEN",
    }[variant.kind]

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


def summarize(rows, group_fields, metric_field="ms"):
    grouped = {}
    for row in rows:
        key = tuple(row[field] for field in group_fields)
        grouped.setdefault(key, []).append(float(row[metric_field]))

    summary = []
    for key, samples in sorted(grouped.items()):
        item = {field: value for field, value in zip(group_fields, key)}
        item["samples"] = len(samples)
        item["median_ms"] = f"{statistics.median(samples):.3f}"
        item["mean_ms"] = f"{statistics.fmean(samples):.3f}"
        item["best_ms"] = f"{min(samples):.3f}"
        total_ops = int(item["total_ops"])
        median_ms = float(item["median_ms"])
        item["median_mops"] = f"{(total_ops / 1_000_000.0) / (median_ms / 1000.0):.3f}"
        if "final_buckets" in row_keys(rows):
            bucket_samples = grouped_extra(rows, group_fields, "final_buckets", key)
            size_samples = grouped_extra(rows, group_fields, "final_size", key)
            item["median_final_buckets"] = str(int(statistics.median(bucket_samples)))
            item["median_final_size"] = str(int(statistics.median(size_samples)))
        summary.append(item)
    return summary


def row_keys(rows):
    return set(rows[0].keys()) if rows else set()


def grouped_extra(rows, group_fields, field, target_key):
    values = []
    for row in rows:
        key = tuple(row[f] for f in group_fields)
        if key == target_key:
            values.append(float(row[field]))
    return values


def write_csv(path: pathlib.Path, rows, fieldnames):
    path.parent.mkdir(exist_ok=True)
    with path.open("w", newline="", encoding="ascii") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def run_general(binary: pathlib.Path, threads: int, total_ops: int, key_space: int, buckets: int, get_pct: int, put_pct: int, remove_pct: int) -> float:
    return float(
        capture(
            [
                str(binary),
                "--threads",
                str(threads),
                "--ops",
                str(total_ops),
                "--keys",
                str(key_space),
                "--buckets",
                str(buckets),
                "--get-pct",
                str(get_pct),
                "--put-pct",
                str(put_pct),
                "--remove-pct",
                str(remove_pct),
            ]
        )
    )


def run_resize(binary: pathlib.Path, threads: int, total_ops: int, buckets: int):
    out = capture(
        [
            str(binary),
            "--threads",
            str(threads),
            "--ops",
            str(total_ops),
            "--buckets",
            str(buckets),
        ]
    )
    ms, final_buckets, final_size = out.split(",")
    return float(ms), int(final_buckets), int(final_size)


def run_workload_mix(repeats: int):
    variants = ALL_VARIANTS
    binaries = {}
    for variant in variants:
        wt = ensure_worktree(variant)
        binaries[variant.name] = compile_benchmark(wt, variant, GENERAL_BENCH_SOURCE, "general_bench")

    mixes = [
        ("read_heavy", 90, 10, 0),
        ("balanced", 70, 20, 10),
        ("write_heavy", 40, 40, 20),
    ]
    thread_counts = [1, 4, 16]
    buckets = 262144
    key_space = 65536
    total_ops = 262144

    rows = []
    for variant in variants:
        for mix_name, get_pct, put_pct, remove_pct in mixes:
            for threads in thread_counts:
                aligned_ops = max(threads, (total_ops // threads) * threads)
                for repeat in range(1, repeats + 1):
                    ms = run_general(
                        binaries[variant.name],
                        threads=threads,
                        total_ops=aligned_ops,
                        key_space=key_space,
                        buckets=buckets,
                        get_pct=get_pct,
                        put_pct=put_pct,
                        remove_pct=remove_pct,
                    )
                    rows.append(
                        {
                            "experiment": "workload_mix",
                            "variant": variant.name,
                            "branch": variant.branch,
                            "mix": mix_name,
                            "get_pct": get_pct,
                            "put_pct": put_pct,
                            "remove_pct": remove_pct,
                            "threads": threads,
                            "buckets": buckets,
                            "key_space": key_space,
                            "total_ops": aligned_ops,
                            "repeat": repeat,
                            "ms": f"{ms:.3f}",
                        }
                    )
                    print(
                        f"{variant.name:>24} mix={mix_name:<11} threads={threads:<2} "
                        f"repeat={repeat}/{repeats} ms={ms:.3f}"
                    )

    summary = summarize(
        rows,
        [
            "experiment",
            "variant",
            "branch",
            "mix",
            "get_pct",
            "put_pct",
            "remove_pct",
            "threads",
            "buckets",
            "key_space",
            "total_ops",
        ],
    )
    return rows, summary


def run_contention(repeats: int):
    variants = ALL_VARIANTS
    binaries = {}
    for variant in variants:
        wt = ensure_worktree(variant)
        binaries[variant.name] = compile_benchmark(wt, variant, GENERAL_BENCH_SOURCE, "general_bench")

    thread_counts = [4, 16]
    key_spaces = [64, 1024, 16384, 65536]
    buckets = 262144
    total_ops = 262144
    get_pct, put_pct, remove_pct = 70, 20, 10

    rows = []
    for variant in variants:
        for threads in thread_counts:
            for key_space in key_spaces:
                aligned_ops = max(threads, (total_ops // threads) * threads)
                for repeat in range(1, repeats + 1):
                    ms = run_general(
                        binaries[variant.name],
                        threads=threads,
                        total_ops=aligned_ops,
                        key_space=key_space,
                        buckets=buckets,
                        get_pct=get_pct,
                        put_pct=put_pct,
                        remove_pct=remove_pct,
                    )
                    rows.append(
                        {
                            "experiment": "contention_sweep",
                            "variant": variant.name,
                            "branch": variant.branch,
                            "threads": threads,
                            "buckets": buckets,
                            "key_space": key_space,
                            "total_ops": aligned_ops,
                            "get_pct": get_pct,
                            "put_pct": put_pct,
                            "remove_pct": remove_pct,
                            "repeat": repeat,
                            "ms": f"{ms:.3f}",
                        }
                    )
                    print(
                        f"{variant.name:>24} contention keys={key_space:<6} threads={threads:<2} "
                        f"repeat={repeat}/{repeats} ms={ms:.3f}"
                    )

    summary = summarize(
        rows,
        [
            "experiment",
            "variant",
            "branch",
            "threads",
            "buckets",
            "key_space",
            "total_ops",
            "get_pct",
            "put_pct",
            "remove_pct",
        ],
    )
    return rows, summary


def run_resize_focus(repeats: int):
    variants = RESIZE_VARIANTS
    binaries = {}
    for variant in variants:
        wt = ensure_worktree(variant)
        binaries[variant.name] = compile_benchmark(wt, variant, RESIZE_BENCH_SOURCE, "resize_bench")

    initial_buckets = [1024, 4096, 16384]
    thread_counts = [1, 8, 16]
    growth_factor = 16

    rows = []
    for variant in variants:
        for buckets in initial_buckets:
            total_ops = buckets * growth_factor
            for threads in thread_counts:
                aligned_ops = max(threads, (total_ops // threads) * threads)
                for repeat in range(1, repeats + 1):
                    ms, final_buckets, final_size = run_resize(
                        binaries[variant.name],
                        threads=threads,
                        total_ops=aligned_ops,
                        buckets=buckets,
                    )
                    rows.append(
                        {
                            "experiment": "resize_focus",
                            "variant": variant.name,
                            "branch": variant.branch,
                            "threads": threads,
                            "initial_buckets": buckets,
                            "total_ops": aligned_ops,
                            "repeat": repeat,
                            "ms": f"{ms:.3f}",
                            "final_buckets": str(final_buckets),
                            "final_size": str(final_size),
                        }
                    )
                    print(
                        f"{variant.name:>24} resize buckets={buckets:<5} threads={threads:<2} "
                        f"repeat={repeat}/{repeats} ms={ms:.3f} final={final_buckets}"
                    )

    summary = summarize(
        rows,
        [
            "experiment",
            "variant",
            "branch",
            "threads",
            "initial_buckets",
            "total_ops",
        ],
    )
    return rows, summary


def main():
    parser = argparse.ArgumentParser(description="Run focused follow-up experiments across concurrent hash map variants.")
    parser.add_argument("--repeats", type=int, default=3, help="repetitions per scenario")
    parser.add_argument(
        "--only",
        default="mix,contention,resize",
        help="comma-separated subset: mix,contention,resize",
    )
    args = parser.parse_args()

    RESULTS_ROOT.mkdir(exist_ok=True)
    selected = {part.strip() for part in args.only.split(",") if part.strip()}

    if "mix" in selected:
        raw, summary = run_workload_mix(args.repeats)
        write_csv(RESULTS_ROOT / "workload_mix_raw.csv", raw, list(raw[0].keys()))
        write_csv(RESULTS_ROOT / "workload_mix_summary.csv", summary, list(summary[0].keys()))

    if "contention" in selected:
        raw, summary = run_contention(args.repeats)
        write_csv(RESULTS_ROOT / "contention_sweep_raw.csv", raw, list(raw[0].keys()))
        write_csv(RESULTS_ROOT / "contention_sweep_summary.csv", summary, list(summary[0].keys()))

    if "resize" in selected:
        raw, summary = run_resize_focus(args.repeats)
        write_csv(RESULTS_ROOT / "resize_focus_raw.csv", raw, list(raw[0].keys()))
        write_csv(RESULTS_ROOT / "resize_focus_summary.csv", summary, list(summary[0].keys()))


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(f"command failed: {' '.join(exc.cmd)}", file=sys.stderr)
        sys.exit(exc.returncode)

#!/usr/bin/env python3
import argparse
import csv
import json
import re
import subprocess
from pathlib import Path

ROW_RE = re.compile(
    r"^\s*(\d+)\s*\|\s*([0-9.]+)\s*\|\s*([0-9.]+)\s*\|\s*([0-9.]+)\s*\|\s*([0-9.]+)\s*\|\s*(\S+)\s*$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run benchmark and save parsed results for the report.")
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--ops", type=int, default=800000)
    parser.add_argument("--buckets", type=int, default=131072)
    parser.add_argument(
        "--output-prefix",
        default="report/data/final_benchmark",
        help="Prefix for generated .txt, .csv, and .json files.",
    )
    return parser.parse_args()


def run_benchmark(threads: int, ops: int, buckets: int) -> str:
    command = [
        "./benchmark",
        "--threads",
        str(threads),
        "--ops",
        str(ops),
        "--buckets",
        str(buckets),
    ]
    completed = subprocess.run(command, check=True, capture_output=True, text=True)
    return completed.stdout


def parse_output(output: str) -> dict:
    metadata = {}
    rows = []
    current_workload = None

    for line in output.splitlines():
        stripped = line.strip()
        if stripped.startswith("Ops:"):
            metadata["ops"] = int(stripped.split()[-1])
        elif stripped.startswith("Buckets:"):
            metadata["buckets"] = int(stripped.split()[-1])
        elif stripped.startswith("Threads:"):
            metadata["max_threads"] = int(stripped.split()[-1])
        elif stripped.startswith("Key space used in each run:"):
            metadata["key_space"] = int(stripped.split()[-1])
        elif stripped.startswith("--- ") and stripped.endswith(" ---"):
            current_workload = stripped[4:-4]
        else:
            match = ROW_RE.match(line)
            if match and current_workload is not None:
                rows.append(
                    {
                        "workload": current_workload,
                        "threads": int(match.group(1)),
                        "locked_ms": float(match.group(2)),
                        "lockfree_ms": float(match.group(3)),
                        "resize_ms": float(match.group(4)),
                        "openaddr_ms": float(match.group(5)),
                        "winner": match.group(6),
                    }
                )

    metadata["rows"] = rows
    return metadata


def write_outputs(prefix: Path, output: str, parsed: dict) -> None:
    prefix.parent.mkdir(parents=True, exist_ok=True)

    raw_path = prefix.with_suffix(".txt")
    csv_path = prefix.with_suffix(".csv")
    json_path = prefix.with_suffix(".json")

    raw_path.write_text(output)

    with csv_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "workload",
                "threads",
                "locked_ms",
                "lockfree_ms",
                "resize_ms",
                "openaddr_ms",
                "winner",
                "ops",
                "buckets",
                "max_threads",
                "key_space",
            ],
        )
        writer.writeheader()
        for row in parsed["rows"]:
            writer.writerow(
                {
                    **row,
                    "ops": parsed.get("ops"),
                    "buckets": parsed.get("buckets"),
                    "max_threads": parsed.get("max_threads"),
                    "key_space": parsed.get("key_space"),
                }
            )

    json_path.write_text(json.dumps(parsed, indent=2))


def main() -> None:
    args = parse_args()
    output = run_benchmark(args.threads, args.ops, args.buckets)
    parsed = parse_output(output)
    prefix = Path(args.output_prefix)
    write_outputs(prefix, output, parsed)

    print(output, end="")
    print(f"\nSaved raw output to {prefix.with_suffix('.txt')}")
    print(f"Saved CSV to {prefix.with_suffix('.csv')}")
    print(f"Saved JSON to {prefix.with_suffix('.json')}")


if __name__ == "__main__":
    main()

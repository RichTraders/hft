#!/usr/bin/env python3
"""
Analyze RDTSC measurement logs from HFT pipeline benchmark.
"""

import argparse
import re
import sys
import math
import glob
import os
from collections import defaultdict
from dataclasses import dataclass

# CPU frequency in Hz
CPU_FREQ_HZ = 3593.234e6


@dataclass
class MeasurementStats:
    name: str
    count: int
    mean: float
    std: float
    p50: float
    p90: float
    p99: float
    min_val: float
    max_val: float


def cycles_to_us(cycles: float) -> float:
    """Convert CPU cycles to microseconds."""
    return cycles / CPU_FREQ_HZ * 1e6


def parse_rdtsc_log(filepath: str, skip: int = 0) -> dict[str, list[int]]:
    """Parse RDTSC log file and extract cycle counts by tag."""
    pattern = re.compile(r'\[RDTSC\]: (\w+): (\d+)')
    measurements = defaultdict(list)
    skip_counts = defaultdict(int)

    with open(filepath, 'r') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                tag = match.group(1)
                cycles = int(match.group(2))
                if skip_counts[tag] < skip:
                    skip_counts[tag] += 1
                    continue
                measurements[tag].append(cycles)

    return measurements


def percentile(sorted_data: list[int], p: float) -> float:
    """Calculate percentile from sorted data."""
    n = len(sorted_data)
    k = (n - 1) * p / 100
    f = int(k)
    c = min(f + 1, n - 1)
    return sorted_data[f] + (k - f) * (sorted_data[c] - sorted_data[f])


def compute_stats(name: str, values: list[int]) -> MeasurementStats:
    """Compute statistics for a measurement."""
    n = len(values)
    mean = sum(values) / n
    variance = sum((x - mean) ** 2 for x in values) / n
    std = math.sqrt(variance)

    sorted_vals = sorted(values)
    return MeasurementStats(
        name=name,
        count=n,
        mean=mean,
        std=std,
        p50=percentile(sorted_vals, 50),
        p90=percentile(sorted_vals, 90),
        p99=percentile(sorted_vals, 99),
        min_val=sorted_vals[0],
        max_val=sorted_vals[-1]
    )


def print_stats(stats: MeasurementStats):
    """Print formatted statistics with both cycles and microseconds."""
    print(f"\n{stats.name} (n={stats.count:,}):")
    print(f"  {'':12} {'Cycles':>14}  {'Microseconds':>12}")
    print(f"  {'Mean:':12} {stats.mean:>14,.0f}  {cycles_to_us(stats.mean):>12.3f} us")
    print(f"  {'Std:':12} {stats.std:>14,.0f}  {cycles_to_us(stats.std):>12.3f} us")
    print(f"  {'P50:':12} {stats.p50:>14,.0f}  {cycles_to_us(stats.p50):>12.3f} us")
    print(f"  {'P90:':12} {stats.p90:>14,.0f}  {cycles_to_us(stats.p90):>12.3f} us")
    print(f"  {'P99:':12} {stats.p99:>14,.0f}  {cycles_to_us(stats.p99):>12.3f} us")
    print(f"  {'Min:':12} {stats.min_val:>14,.0f}  {cycles_to_us(stats.min_val):>12.3f} us")
    print(f"  {'Max:':12} {stats.max_val:>14,.0f}  {cycles_to_us(stats.max_val):>12.3f} us")


def find_latest_log() -> str:
    """Find the most recent benchmark log file."""
    pattern = "benchmark_rdtsc_*.log"
    files = glob.glob(pattern)
    if not files:
        return "benchmark_rdtsc.log"
    return max(files, key=os.path.getmtime)


def main():
    global CPU_FREQ_HZ

    parser = argparse.ArgumentParser(description='Analyze RDTSC measurement logs')
    parser.add_argument('logfile', nargs='?', help='Log file to analyze (default: latest)')
    parser.add_argument('--skip', '-s', type=int, default=0,
                        help='Number of initial samples to skip per tag (warmup)')
    parser.add_argument('--freq', '-f', type=float, default=CPU_FREQ_HZ,
                        help=f'CPU frequency in Hz (default: {CPU_FREQ_HZ:.3e})')
    args = parser.parse_args()

    CPU_FREQ_HZ = args.freq

    if args.logfile:
        filepath = args.logfile
    else:
        filepath = find_latest_log()
        print(f"Auto-detected latest log: {filepath}")

    print(f"Parsing: {filepath}")
    if args.skip > 0:
        print(f"Skipping first {args.skip} samples per tag (warmup)")

    measurements = parse_rdtsc_log(filepath, skip=args.skip)

    if not measurements:
        print("No RDTSC measurements found!")
        return 1

    print(f"\nCPU Frequency: {CPU_FREQ_HZ:.3e} Hz ({CPU_FREQ_HZ/1e9:.3f} GHz)")
    print(f"\nFound {len(measurements)} measurement types:")
    for name, values in sorted(measurements.items()):
        print(f"  {name}: {len(values):,} samples")

    print("\n" + "=" * 70)
    print("RDTSC Cycle Statistics")
    print("=" * 70)

    # Sort by importance
    priority_order = [
        'Convert_Message_Stream',
        'Convert_Message_API',
        'ORDERBOOK_UPDATED',
        'TRADE_UPDATED',
        'BOOK_TICKER_UPDATED',
        'MAKE_ORDERBOOK_UNIT',
        'MAKE_ORDERBOOK_ALL',
    ]

    # Print priority tags first
    for tag in priority_order:
        if tag in measurements:
            stats = compute_stats(tag, measurements[tag])
            print_stats(stats)

    # Print remaining tags
    for tag in sorted(measurements.keys()):
        if tag not in priority_order:
            stats = compute_stats(tag, measurements[tag])
            print_stats(stats)

    return 0


if __name__ == "__main__":
    sys.exit(main())

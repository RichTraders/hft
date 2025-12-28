#!/usr/bin/env python3
"""
Analyze RDTSC measurement logs from HFT pipeline benchmark.

Supports:
- Single file: python analyze_rdtsc.py benchmark.log
- Multiple files with suffix: python analyze_rdtsc.py benchmark_20251225230026
  (auto-finds _1.log, _2.log, _3.log, .log and merges in time order)
- Glob pattern: python analyze_rdtsc.py "benchmark_*.log"
"""

import argparse
import re
import sys
import math
import glob
import os
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from typing import Iterator, Tuple

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


@dataclass
class TimestampedMeasurement:
    timestamp: datetime
    tag: str
    cycles: int


def cycles_to_us(cycles: float) -> float:
    """Convert CPU cycles to microseconds."""
    return cycles / CPU_FREQ_HZ * 1e6


def parse_timestamp(line: str) -> datetime | None:
    """Parse ISO timestamp from log line like [2025-12-25T14:00:26.069206Z]."""
    match = re.match(r'\[(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+)Z?\]', line)
    if match:
        ts_str = match.group(1)
        # Handle variable microsecond precision
        if '.' in ts_str:
            base, frac = ts_str.split('.')
            frac = frac[:6].ljust(6, '0')  # Normalize to 6 digits
            ts_str = f"{base}.{frac}"
        return datetime.strptime(ts_str, "%Y-%m-%dT%H:%M:%S.%f")
    return None


def find_related_files(base_path: str) -> list[str]:
    """
    Find all related log files for a given base path.

    Examples:
    - "benchmark_20251225230026" -> finds _1.log, _2.log, _3.log, .log
    - "benchmark_20251225230026.log" -> finds _1.log, _2.log, _3.log, .log
    - "benchmark_*.log" -> glob pattern
    """
    # Remove .log extension if present
    if base_path.endswith('.log'):
        base_path = base_path[:-4]

    # Check if it's a glob pattern
    if '*' in base_path or '?' in base_path:
        return sorted(glob.glob(base_path if base_path.endswith('.log') else f"{base_path}.log"))

    # Find all related files: base.log, base_1.log, base_2.log, ...
    files = []

    # Main file (no suffix)
    main_file = f"{base_path}.log"
    if os.path.exists(main_file):
        files.append(main_file)

    # Numbered suffixes
    i = 1
    while True:
        suffix_file = f"{base_path}_{i}.log"
        if os.path.exists(suffix_file):
            files.append(suffix_file)
            i += 1
        else:
            break

    return files


def iter_file_lines_with_timestamp(filepath: str) -> Iterator[Tuple[datetime, str]]:
    """Iterate over file lines, yielding (timestamp, line) tuples."""
    with open(filepath, 'r') as f:
        for line in f:
            ts = parse_timestamp(line)
            if ts:
                yield (ts, line)


def merge_files_by_time(filepaths: list[str]) -> Iterator[str]:
    """
    Merge multiple log files by timestamp order.
    Uses a min-heap to efficiently merge sorted streams.
    """
    import heapq

    # Create iterators for each file
    iterators = []
    for fp in filepaths:
        it = iter_file_lines_with_timestamp(fp)
        try:
            ts, line = next(it)
            # heapq uses first element for comparison
            iterators.append((ts, line, it, fp))
        except StopIteration:
            pass

    # Use heap to merge
    heapq.heapify(iterators)

    while iterators:
        ts, line, it, fp = heapq.heappop(iterators)
        yield line

        try:
            next_ts, next_line = next(it)
            heapq.heappush(iterators, (next_ts, next_line, it, fp))
        except StopIteration:
            pass


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


def parse_rdtsc_from_lines(lines: Iterator[str], skip: int = 0) -> dict[str, list[int]]:
    """Parse RDTSC measurements from an iterator of lines."""
    pattern = re.compile(r'\[RDTSC\]: (\w+): (\d+)')
    measurements = defaultdict(list)
    skip_counts = defaultdict(int)

    for line in lines:
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


def print_markdown_table(all_stats: list[MeasurementStats]):
    """Print statistics as a markdown table."""
    print("\n## RDTSC Latency Summary (microseconds)\n")
    print("| Measurement | Count | Mean | Std | P50 | P90 | P99 | Min | Max |")
    print("|-------------|------:|-----:|----:|----:|----:|----:|----:|----:|")
    for stats in all_stats:
        print(f"| {stats.name} | {stats.count:,} | "
              f"{cycles_to_us(stats.mean):.3f} | "
              f"{cycles_to_us(stats.std):.3f} | "
              f"{cycles_to_us(stats.p50):.3f} | "
              f"{cycles_to_us(stats.p90):.3f} | "
              f"{cycles_to_us(stats.p99):.3f} | "
              f"{cycles_to_us(stats.min_val):.3f} | "
              f"{cycles_to_us(stats.max_val):.3f} |")


def find_latest_log_group() -> str:
    """
    Find the most recent benchmark log file group.
    Returns the base name (without _N suffix) of the latest log group.
    """
    pattern = "benchmark_rdtsc_*.log"
    files = glob.glob(pattern)
    if not files:
        return "benchmark_rdtsc.log"

    # Group files by base name (remove _N suffix)
    base_names = set()
    for f in files:
        # Remove .log extension
        name = f[:-4] if f.endswith('.log') else f
        # Remove _N suffix if present
        match = re.match(r'(.+?)(?:_\d+)?$', name)
        if match:
            base_names.add(match.group(1))

    if not base_names:
        return max(files, key=os.path.getmtime)

    # Find the latest base name by checking any of its files
    latest_base = None
    latest_mtime = 0
    for base in base_names:
        related = find_related_files(base)
        if related:
            mtime = max(os.path.getmtime(f) for f in related)
            if mtime > latest_mtime:
                latest_mtime = mtime
                latest_base = base

    return latest_base if latest_base else "benchmark_rdtsc.log"


def main():
    global CPU_FREQ_HZ

    parser = argparse.ArgumentParser(
        description='Analyze RDTSC measurement logs',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s benchmark.log                    # Single file
  %(prog)s benchmark_20251225230026         # Merge _1.log, _2.log, etc. by time
  %(prog)s benchmark_20251225230026.log     # Same as above
  %(prog)s --skip 100 benchmark.log         # Skip first 100 warmup samples
        """
    )
    parser.add_argument('logfile', nargs='?',
                        help='Log file or base name (default: latest)')
    parser.add_argument('--skip', '-s', type=int, default=0,
                        help='Number of initial samples to skip per tag (warmup)')
    parser.add_argument('--freq', '-f', type=float, default=CPU_FREQ_HZ,
                        help=f'CPU frequency in Hz (default: {CPU_FREQ_HZ:.3e})')
    parser.add_argument('--no-merge', action='store_true',
                        help='Do not merge related files, parse single file only')
    parser.add_argument('--markdown', '-m', action='store_true',
                        help='Output as markdown table')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Output file for markdown table')
    args = parser.parse_args()

    CPU_FREQ_HZ = args.freq

    if args.logfile:
        base_path = args.logfile
    else:
        base_path = find_latest_log_group()
        print(f"Auto-detected latest log group: {base_path}")

    # Find related files
    if args.no_merge:
        # Single file mode
        if not base_path.endswith('.log'):
            base_path = f"{base_path}.log"
        if not os.path.exists(base_path):
            print(f"Error: File not found: {base_path}")
            return 1
        files = [base_path]
    else:
        files = find_related_files(base_path)

    if not files:
        print(f"Error: No files found for: {base_path}")
        return 1

    print(f"Found {len(files)} log file(s):")
    total_size = 0
    for f in files:
        size = os.path.getsize(f)
        total_size += size
        print(f"  {f} ({size / 1024 / 1024:.1f} MB)")
    print(f"Total size: {total_size / 1024 / 1024:.1f} MB")

    if args.skip > 0:
        print(f"Skipping first {args.skip} samples per tag (warmup)")

    # Parse measurements
    if len(files) == 1:
        print(f"\nParsing single file...")
        measurements = parse_rdtsc_log(files[0], skip=args.skip)
    else:
        print(f"\nMerging {len(files)} files by timestamp...")
        merged_lines = merge_files_by_time(files)
        measurements = parse_rdtsc_from_lines(merged_lines, skip=args.skip)

    if not measurements:
        print("No RDTSC measurements found!")
        return 1

    # Merge MAKE_ORDERBOOK_UNIT_BID and MAKE_ORDERBOOK_UNIT_ASK into MAKE_ORDERBOOK_UNIT
    unit_bid = measurements.pop('MAKE_ORDERBOOK_UNIT_BID', [])
    unit_ask = measurements.pop('MAKE_ORDERBOOK_UNIT_ASK', [])
    if unit_bid or unit_ask:
        measurements['MAKE_ORDERBOOK_UNIT'].extend(unit_bid)
        measurements['MAKE_ORDERBOOK_UNIT'].extend(unit_ask)

    print(f"\nCPU Frequency: {CPU_FREQ_HZ:.3e} Hz ({CPU_FREQ_HZ/1e9:.3f} GHz)")
    print(f"\nFound {len(measurements)} measurement types:")
    for name, values in sorted(measurements.items()):
        print(f"  {name}: {len(values):,} samples")

    priority_order = [
        'Convert_Message_Stream',
        'Convert_Message_API',
        'ORDERBOOK_UPDATED',
        'TRADE_UPDATED',
        'BOOK_TICKER_UPDATED',
        'MAKE_ORDERBOOK_UNIT',
        'MAKE_ORDERBOOK_ALL',
    ]

    all_stats = []
    for tag in priority_order:
        if tag in measurements:
            all_stats.append(compute_stats(tag, measurements[tag]))
    for tag in sorted(measurements.keys()):
        if tag not in priority_order:
            all_stats.append(compute_stats(tag, measurements[tag]))

    if args.markdown or args.output:
        import io
        from contextlib import redirect_stdout

        if args.output:
            with open(args.output, 'w') as f:
                with redirect_stdout(f):
                    print_markdown_table(all_stats)
            print(f"Markdown table saved to: {args.output}")
        else:
            print_markdown_table(all_stats)
    else:
        print("\n" + "=" * 70)
        print("RDTSC Cycle Statistics")
        print("=" * 70)
        for stats in all_stats:
            print_stats(stats)

    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Plot mid-price with regime classification (Up/Down/Sideways) and Phase tracking.

Features:
1. Regime Detection (price-based time slices)
2. Phase Tracking (from log transitions)
3. Subplot: Regime (top) + Phase (bottom)
4. Parquet export for later analysis
"""

import re
import sys
import os
import glob
import argparse
import heapq
import matplotlib.pyplot as plt
from datetime import datetime, timezone
from collections import namedtuple
from pathlib import Path
from typing import Iterator, Tuple

BookTicker = namedtuple('BookTicker', ['timestamp_ms', 'bid', 'ask', 'mid'])
TradeEvent = namedtuple('TradeEvent', ['timestamp_ms', 'price'])
PhaseTransition = namedtuple('PhaseTransition', ['timestamp_ms', 'side', 'from_phase', 'to_phase'])

# Phase enum mapping
PHASES = ['NEUTRAL', 'BUILDING', 'DEEP', 'WEAK', 'VERY_WEAK']
PHASE_COLORS = {
    'NEUTRAL': '#D3D3D3',    # gray
    'BUILDING': '#FFD700',   # gold
    'DEEP': '#FF4500',       # orange-red
    'WEAK': '#87CEEB',       # sky blue
    'VERY_WEAK': '#90EE90',  # light green
}

# Regime colors
REGIME_COLORS = {
    'up': '#90EE90',      # light green
    'down': '#FFB6C1',    # light red/pink
    'sideways': '#D3D3D3'  # light gray
}


def find_related_files(base_path: str) -> list[str]:
    """
    Find all related log files for a given base path.
    Files with _N suffix come first (in order), file without suffix comes last.

    Examples:
    - "benchmark_20251225230026" -> finds _1.log, _2.log, _3.log, then .log
    - "benchmark_20251225230026.log" -> same as above
    """
    # Remove .log extension if present
    if base_path.endswith('.log'):
        base_path = base_path[:-4]

    # Check if it's a glob pattern
    if '*' in base_path or '?' in base_path:
        return sorted(glob.glob(base_path if base_path.endswith('.log') else f"{base_path}.log"))

    # Find all related files: base_1.log, base_2.log, ..., base.log (no suffix last)
    files = []

    # Numbered suffixes first (in order: _1, _2, _3, ...)
    i = 1
    while True:
        suffix_file = f"{base_path}_{i}.log"
        if os.path.exists(suffix_file):
            files.append(suffix_file)
            i += 1
        else:
            break

    # Main file (no suffix) comes last
    main_file = f"{base_path}.log"
    if os.path.exists(main_file):
        files.append(main_file)

    return files


def parse_timestamp(line: str) -> datetime | None:
    """Parse ISO timestamp from log line like [2025-12-25T14:00:26.069206Z]."""
    match = re.match(r'\[(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+)Z?\]', line)
    if match:
        ts_str = match.group(1)
        if '.' in ts_str:
            base, frac = ts_str.split('.')
            frac = frac[:6].ljust(6, '0')
            ts_str = f"{base}.{frac}"
        return datetime.strptime(ts_str, "%Y-%m-%dT%H:%M:%S.%f")
    return None


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
    iterators = []
    counter = 0
    for fp in filepaths:
        it = iter_file_lines_with_timestamp(fp)
        try:
            ts, line = next(it)
            iterators.append((ts, counter, line, it, fp))
            counter += 1
        except StopIteration:
            pass

    heapq.heapify(iterators)

    while iterators:
        ts, idx, line, it, fp = heapq.heappop(iterators)
        yield line

        try:
            next_ts, next_line = next(it)
            heapq.heappush(iterators, (next_ts, idx, next_line, it, fp))
        except StopIteration:
            pass


def parse_book_tickers_from_lines(lines: Iterator[str]) -> list[BookTicker]:
    """Parse BookTickerEvent entries from lines iterator."""
    pattern = re.compile(
        r'BookTickerEvent E:(\d+) T:\d+ bid:(\d+)@\d+ ask:(\d+)@\d+'
    )

    tickers = []
    for line in lines:
        match = pattern.search(line)
        if match:
            ts = int(match.group(1))
            bid = int(match.group(2))
            ask = int(match.group(3))
            mid = (bid + ask) / 2
            tickers.append(BookTicker(ts, bid, ask, mid))

    return tickers


def parse_book_tickers(filepath: str) -> list[BookTicker]:
    """Parse BookTickerEvent entries from log file."""
    with open(filepath, 'r') as f:
        return parse_book_tickers_from_lines(f)


def parse_trade_events_from_lines(lines: Iterator[str]) -> list[TradeEvent]:
    """Parse TradeEvent entries from lines iterator."""
    pattern = re.compile(
        r'TradeEvent E:(\d+) T:\d+ data:(\d+)'
    )

    trades = []
    for line in lines:
        match = pattern.search(line)
        if match:
            ts = int(match.group(1))
            price = int(match.group(2))
            trades.append(TradeEvent(ts, price))

    return trades


def parse_trade_events(filepath: str) -> list[TradeEvent]:
    """Parse TradeEvent entries from log file."""
    with open(filepath, 'r') as f:
        return parse_trade_events_from_lines(f)


def parse_phase_transitions_from_lines(lines: Iterator[str], tickers: list[BookTicker]) -> list[PhaseTransition]:
    """
    Parse Phase transitions from lines iterator.
    Use the E timestamp from the immediately preceding BookTickerEvent.
    """
    ticker_pattern = re.compile(r'BookTickerEvent E:(\d+)')
    phase_pattern = re.compile(r'\[Phase (LONG|SHORT)\] Transition: (\w+) → (\w+)')

    transitions = []
    last_e_ts = tickers[0].timestamp_ms if tickers else 0

    for line in lines:
        ticker_match = ticker_pattern.search(line)
        if ticker_match:
            last_e_ts = int(ticker_match.group(1))
            continue

        phase_match = phase_pattern.search(line)
        if phase_match:
            side = phase_match.group(1)
            from_phase = phase_match.group(2)
            to_phase = phase_match.group(3)
            transitions.append(PhaseTransition(last_e_ts, side, from_phase, to_phase))

    return transitions


def parse_phase_transitions(filepath: str, tickers: list[BookTicker]) -> list[PhaseTransition]:
    """Parse Phase transitions from log file."""
    with open(filepath, 'r') as f:
        return parse_phase_transitions_from_lines(f, tickers)


def build_phase_timeline(transitions: list[PhaseTransition],
                         start_ts: int, end_ts: int,
                         side: str = 'LONG') -> list[tuple]:
    """
    Build phase timeline from transitions.
    Returns: list of (start_ts, end_ts, phase) tuples
    """
    # Filter by side
    side_transitions = [t for t in transitions if t.side == side]

    if not side_transitions:
        return [(start_ts, end_ts, 'NEUTRAL')]

    timeline = []
    current_phase = 'NEUTRAL'
    current_start = start_ts

    for t in side_transitions:
        if t.timestamp_ms > current_start:
            timeline.append((current_start, t.timestamp_ms, current_phase))
        current_phase = t.to_phase
        current_start = t.timestamp_ms

    # Add final segment
    if current_start < end_ts:
        timeline.append((current_start, end_ts, current_phase))

    return timeline


def classify_regimes(tickers: list[BookTicker],
                     slice_ms: int = 1000,
                     threshold_bps: float = 5.0) -> list[tuple]:
    """
    Classify each time slice into regime based on which wall is hit first.
    Returns: list of (start_ts, end_ts, regime) tuples
    """
    if not tickers:
        return []

    regimes = []
    start_ts = tickers[0].timestamp_ms
    end_ts = tickers[-1].timestamp_ms

    slice_start = start_ts
    while slice_start < end_ts:
        slice_end = slice_start + slice_ms

        slice_tickers = [t for t in tickers
                         if slice_start <= t.timestamp_ms < slice_end]

        if not slice_tickers:
            slice_start = slice_end
            continue

        ref_price = slice_tickers[0].mid
        upper_wall = ref_price * (1 + threshold_bps / 10000)
        lower_wall = ref_price * (1 - threshold_bps / 10000)

        regime = 'sideways'
        for t in slice_tickers:
            if t.mid >= upper_wall:
                regime = 'up'
                break
            elif t.mid <= lower_wall:
                regime = 'down'
                break

        actual_end = min(slice_end, slice_tickers[-1].timestamp_ms)
        regimes.append((slice_start, actual_end, regime))
        slice_start = slice_end

    return regimes


def plot_regime_only(ax, tickers: list[BookTicker], regimes: list[tuple], title: str):
    """Plot regime subplot."""
    times = [t.timestamp_ms for t in tickers]
    mids = [t.mid / 1000000 for t in tickers]  # Scale to actual price

    ax.plot(times, mids, 'b-', linewidth=0.5, alpha=0.8, label='Mid Price')

    for start_ts, end_ts, regime in regimes:
        ax.axvspan(start_ts, end_ts, alpha=0.3, color=REGIME_COLORS[regime])

    ax.set_ylabel('Mid Price', fontsize=10)
    ax.set_title(title, fontsize=11)
    ax.grid(True, alpha=0.3)

    # Format x-axis
    def format_func(x, pos):
        dt = datetime.fromtimestamp(x / 1000, tz=timezone.utc)
        return dt.strftime('%H:%M:%S')
    ax.xaxis.set_major_formatter(plt.FuncFormatter(format_func))


def plot_phase_only(ax, trades: list[TradeEvent], phase_timeline: list[tuple],
                    title: str, side: str):
    """Plot phase subplot with trade price."""
    times = [t.timestamp_ms for t in trades]
    prices = [t.price / 1000000 for t in trades]  # Scale to actual price

    ax.plot(times, prices, 'b-', linewidth=0.5, alpha=0.8, label='Trade Price')

    for start_ts, end_ts, phase in phase_timeline:
        color = PHASE_COLORS.get(phase, '#FFFFFF')
        ax.axvspan(start_ts, end_ts, alpha=0.3, color=color)

    ax.set_xlabel('Time (UTC)', fontsize=10)
    ax.set_ylabel('Trade Price', fontsize=10)
    ax.set_title(f'{title} ({side})', fontsize=11)
    ax.grid(True, alpha=0.3)

    def format_func(x, pos):
        dt = datetime.fromtimestamp(x / 1000, tz=timezone.utc)
        return dt.strftime('%H:%M:%S')
    ax.xaxis.set_major_formatter(plt.FuncFormatter(format_func))


def plot_combined(tickers: list[BookTicker],
                  trades: list[TradeEvent],
                  regimes: list[tuple],
                  phase_timeline_long: list[tuple],
                  phase_timeline_short: list[tuple],
                  output_path: str = None,
                  slice_ms: int = 1000,
                  threshold_bps: float = 5.0):
    """Plot combined subplot: Regime (top) + Phase LONG (middle) + Phase SHORT (bottom)."""
    from matplotlib.patches import Patch

    fig, axes = plt.subplots(3, 1, figsize=(16, 12), sharex=True)

    # Top: Regime (mid price)
    plot_regime_only(axes[0], tickers, regimes,
                     f'Regime Classification (slice={slice_ms}ms, threshold={threshold_bps}bps)')

    # Middle: Phase LONG (trade price)
    plot_phase_only(axes[1], trades, phase_timeline_long, 'Phase Timeline', 'LONG')

    # Bottom: Phase SHORT (trade price)
    plot_phase_only(axes[2], trades, phase_timeline_short, 'Phase Timeline', 'SHORT')

    # Create legends
    regime_legend = [
        Patch(facecolor=REGIME_COLORS['up'], alpha=0.3, label='Up'),
        Patch(facecolor=REGIME_COLORS['down'], alpha=0.3, label='Down'),
        Patch(facecolor=REGIME_COLORS['sideways'], alpha=0.3, label='Sideways'),
    ]
    axes[0].legend(handles=regime_legend, loc='upper right', fontsize=8)

    phase_legend = [
        Patch(facecolor=PHASE_COLORS[p], alpha=0.3, label=p) for p in PHASES
    ]
    axes[1].legend(handles=phase_legend, loc='upper right', fontsize=8)
    axes[2].legend(handles=phase_legend, loc='upper right', fontsize=8)

    plt.tight_layout()
    plt.xticks(rotation=45)

    if output_path:
        plt.savefig(output_path, dpi=150)
        print(f"Saved plot to {output_path}")
    else:
        plt.show()


def export_to_parquet(tickers: list[BookTicker],
                      regimes: list[tuple],
                      phase_timeline_long: list[tuple],
                      phase_timeline_short: list[tuple],
                      output_path: str):
    """Export regime and phase data to parquet file."""
    try:
        import pandas as pd
    except ImportError:
        print("pandas not installed, skipping parquet export")
        return

    # Build DataFrame with regime and phase at each ticker timestamp
    records = []

    # Pre-index regimes and phases for faster lookup
    def find_label(ts, timeline):
        for start, end, label in timeline:
            if start <= ts < end:
                return label
        return timeline[-1][2] if timeline else 'UNKNOWN'

    for t in tickers:
        regime = find_label(t.timestamp_ms, regimes)
        phase_long = find_label(t.timestamp_ms, phase_timeline_long)
        phase_short = find_label(t.timestamp_ms, phase_timeline_short)

        records.append({
            'timestamp_ms': t.timestamp_ms,
            'timestamp_utc': datetime.fromtimestamp(t.timestamp_ms / 1000, tz=timezone.utc),
            'bid': t.bid,
            'ask': t.ask,
            'mid': t.mid,
            'regime': regime,
            'phase_long': phase_long,
            'phase_short': phase_short,
        })

    df = pd.DataFrame(records)
    df.to_parquet(output_path, index=False)
    print(f"Exported {len(df)} records to {output_path}")

    # Print summary
    print("\n=== Parquet Summary ===")
    print(f"Regime distribution:\n{df['regime'].value_counts()}")
    print(f"\nPhase LONG distribution:\n{df['phase_long'].value_counts()}")
    print(f"\nPhase SHORT distribution:\n{df['phase_short'].value_counts()}")


def print_regime_stats(regimes: list[tuple]):
    """Print statistics about regime distribution."""
    counts = {'up': 0, 'down': 0, 'sideways': 0}
    durations = {'up': 0, 'down': 0, 'sideways': 0}

    for start_ts, end_ts, regime in regimes:
        counts[regime] += 1
        durations[regime] += (end_ts - start_ts)

    total = sum(counts.values())
    total_duration = sum(durations.values())

    print("\n=== Regime Statistics ===")
    print(f"Total slices: {total}")
    print(f"Total duration: {total_duration/1000:.1f} seconds\n")

    for regime in ['up', 'down', 'sideways']:
        pct = counts[regime] / total * 100 if total > 0 else 0
        dur_pct = durations[regime] / total_duration * 100 if total_duration > 0 else 0
        print(f"{regime.upper():>10}: {counts[regime]:4d} slices ({pct:5.1f}%) | "
              f"{durations[regime]/1000:7.1f}s ({dur_pct:5.1f}%)")


def print_phase_stats(phase_timeline: list[tuple], side: str):
    """Print statistics about phase distribution."""
    counts = {p: 0 for p in PHASES}
    durations = {p: 0 for p in PHASES}

    for start_ts, end_ts, phase in phase_timeline:
        if phase in counts:
            counts[phase] += 1
            durations[phase] += (end_ts - start_ts)

    total = sum(counts.values())
    total_duration = sum(durations.values())

    print(f"\n=== Phase Statistics ({side}) ===")
    print(f"Total segments: {total}")
    print(f"Total duration: {total_duration/1000:.1f} seconds\n")

    for phase in PHASES:
        pct = counts[phase] / total * 100 if total > 0 else 0
        dur_pct = durations[phase] / total_duration * 100 if total_duration > 0 else 0
        print(f"{phase:>10}: {counts[phase]:4d} segments ({pct:5.1f}%) | "
              f"{durations[phase]/1000:7.1f}s ({dur_pct:5.1f}%)")


def parse_all_from_files(files: list[str]) -> tuple[list[BookTicker], list[TradeEvent], list[PhaseTransition]]:
    """
    Parse all data from multiple files, merging by timestamp.
    Returns (tickers, trades, transitions) - transitions need tickers for timestamp lookup.
    """
    if len(files) == 1:
        # Single file - simple path
        tickers = parse_book_tickers(files[0])
        trades = parse_trade_events(files[0])
        transitions = parse_phase_transitions(files[0], tickers)
        return tickers, trades, transitions

    # Multiple files - need to merge and parse together
    # We need to read files multiple times or cache lines
    print(f"Merging {len(files)} files by timestamp...")

    # Collect all lines first (for multiple passes)
    all_lines = list(merge_files_by_time(files))
    print(f"Total merged lines: {len(all_lines)}")

    # Parse each type from merged lines
    tickers = parse_book_tickers_from_lines(iter(all_lines))
    trades = parse_trade_events_from_lines(iter(all_lines))
    transitions = parse_phase_transitions_from_lines(iter(all_lines), tickers)

    return tickers, trades, transitions


def main():
    parser = argparse.ArgumentParser(
        description='Plot regime and phase classification from log file',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s benchmark.log                    # Single file
  %(prog)s benchmark_20251225230026         # Merge _1.log, _2.log, etc. by time
  %(prog)s benchmark_20251225230026.log     # Same as above
        """
    )
    parser.add_argument('logfile', help='Path to log file or base name (auto-finds related files)')
    parser.add_argument('--slice-ms', type=int, default=10000,
                        help='Time slice duration in milliseconds (default: 10000)')
    parser.add_argument('--threshold-bps', type=float, default=2.0,
                        help='Price threshold in basis points (default: 2.0)')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Output image file path (default: show plot)')
    parser.add_argument('--parquet', '-p', type=str, default=None,
                        help='Output parquet file path for data export')
    parser.add_argument('--stats', action='store_true',
                        help='Print regime/phase statistics')
    parser.add_argument('--regime-only', action='store_true',
                        help='Plot only regime (no phase)')
    parser.add_argument('--no-merge', action='store_true',
                        help='Do not merge related files, parse single file only')

    args = parser.parse_args()

    # Find related files
    if args.no_merge:
        if not args.logfile.endswith('.log'):
            args.logfile = f"{args.logfile}.log"
        if not os.path.exists(args.logfile):
            print(f"Error: File not found: {args.logfile}")
            return 1
        files = [args.logfile]
    else:
        files = find_related_files(args.logfile)

    if not files:
        print(f"Error: No files found for: {args.logfile}")
        return 1

    print(f"Found {len(files)} log file(s):")
    total_size = 0
    for f in files:
        size = os.path.getsize(f)
        total_size += size
        print(f"  {f} ({size / 1024 / 1024:.1f} MB)")
    print(f"Total size: {total_size / 1024 / 1024:.1f} MB")

    # Parse all data
    print("\nParsing log files...")
    tickers, trades, transitions = parse_all_from_files(files)
    print(f"Found {len(tickers)} BookTickerEvent entries")

    if not tickers:
        print("No data found!")
        return 1

    start_ts = tickers[0].timestamp_ms
    end_ts = tickers[-1].timestamp_ms
    print(f"Time range: {start_ts} - {end_ts}")
    print(f"Duration: {(end_ts - start_ts)/1000:.1f} seconds")

    # Classify regimes
    print(f"\nClassifying regimes (slice={args.slice_ms}ms, threshold={args.threshold_bps}bps)...")
    regimes = classify_regimes(tickers, args.slice_ms, args.threshold_bps)
    print(f"Generated {len(regimes)} regime slices")

    if args.stats:
        print_regime_stats(regimes)

    # Build phase timelines
    if not args.regime_only:
        print(f"Found {len(trades)} TradeEvent entries")
        print(f"Found {len(transitions)} phase transitions")

        phase_timeline_long = build_phase_timeline(transitions, start_ts, end_ts, 'LONG')
        phase_timeline_short = build_phase_timeline(transitions, start_ts, end_ts, 'SHORT')
        print(f"Built phase timeline: LONG={len(phase_timeline_long)}, SHORT={len(phase_timeline_short)}")

        if args.stats:
            print_phase_stats(phase_timeline_long, 'LONG')
            print_phase_stats(phase_timeline_short, 'SHORT')

        # Export to parquet
        if args.parquet:
            export_to_parquet(tickers, regimes, phase_timeline_long, phase_timeline_short,
                              args.parquet)

        # Plot combined
        plot_combined(tickers, trades, regimes, phase_timeline_long, phase_timeline_short,
                      args.output, args.slice_ms, args.threshold_bps)
    else:
        # Plot regime only (original behavior)
        from matplotlib.patches import Patch

        fig, ax = plt.subplots(figsize=(16, 8))
        plot_regime_only(ax, tickers, regimes,
                         f'Regime Classification (slice={args.slice_ms}ms, threshold={args.threshold_bps}bps)')

        regime_legend = [
            Patch(facecolor=REGIME_COLORS['up'], alpha=0.3, label='Up'),
            Patch(facecolor=REGIME_COLORS['down'], alpha=0.3, label='Down'),
            Patch(facecolor=REGIME_COLORS['sideways'], alpha=0.3, label='Sideways'),
        ]
        ax.legend(handles=regime_legend, loc='upper right')

        plt.tight_layout()
        plt.xticks(rotation=45)

        if args.output:
            plt.savefig(args.output, dpi=150)
            print(f"Saved plot to {args.output}")
        else:
            plt.show()

    return 0


if __name__ == '__main__':
    sys.exit(main())

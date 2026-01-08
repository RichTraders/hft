#!/usr/bin/env python3
"""
Analyze Phase prediction accuracy against Regime ground truth.

Trading-oriented confusion matrix:
- Rows: Predicted action (Long Entry, Hold, Short Entry)
- Cols: Actual regime (Up, Sideways, Down)

Phase to Action mapping:
- LONG VERY_WEAK -> Long Entry (reversal signal)
- SHORT VERY_WEAK -> Short Entry (reversal signal)
- Otherwise -> Hold (wait for entry signal)
"""

import argparse
import sys
from collections import defaultdict

import pandas as pd
import numpy as np


def load_parquet(filepath: str) -> pd.DataFrame:
    """Load parquet file and compute time deltas."""
    df = pd.read_parquet(filepath)

    # Sort by timestamp
    df = df.sort_values('timestamp_ms').reset_index(drop=True)

    # Compute duration for each tick (time until next tick)
    df['duration_ms'] = df['timestamp_ms'].diff().shift(-1).fillna(0)

    return df


def map_phase_to_action(row) -> str:
    """
    Map Phase LONG/SHORT to trading action.

    Logic:
    - LONG VERY_WEAK -> 'long_entry' (buy signal - expect reversal up)
    - SHORT VERY_WEAK -> 'short_entry' (sell signal - expect reversal down)
    - Both VERY_WEAK -> use the one that transitioned more recently (ambiguous, default hold)
    - Otherwise -> 'hold' (no entry signal yet)
    """
    phase_long = row['phase_long']
    phase_short = row['phase_short']

    long_entry = phase_long == 'VERY_WEAK'
    short_entry = phase_short == 'VERY_WEAK'

    if long_entry and not short_entry:
        return 'long_entry'
    elif short_entry and not long_entry:
        return 'short_entry'
    elif long_entry and short_entry:
        # Both VERY_WEAK - ambiguous, default to hold
        return 'hold'
    else:
        return 'hold'


def compute_trading_matrix(df: pd.DataFrame) -> dict:
    """
    Compute time-weighted trading confusion matrix.

    Returns dict with:
    - matrix[action][regime] = duration_ms
    """
    # Add action column
    df['action'] = df.apply(map_phase_to_action, axis=1)

    actions = ['long_entry', 'hold', 'short_entry']
    regimes = ['up', 'sideways', 'down']
    matrix = defaultdict(lambda: defaultdict(float))

    # Accumulate durations
    for _, row in df.iterrows():
        action = row['action']
        regime = row['regime']
        duration = row['duration_ms']
        matrix[action][regime] += duration

    return dict(matrix), df


def print_trading_matrix(matrix: dict, total_duration_ms: float):
    """Print trading-oriented confusion matrix."""
    actions = ['long_entry', 'hold', 'short_entry']
    regimes = ['up', 'sideways', 'down']

    # Labels for cells
    labels = {
        ('long_entry', 'up'): 'TP (Profit)',
        ('long_entry', 'sideways'): 'FP (Cost)',
        ('long_entry', 'down'): 'FATAL (Loss)',
        ('hold', 'up'): 'Missed',
        ('hold', 'sideways'): 'TN (Good)',
        ('hold', 'down'): 'Missed',
        ('short_entry', 'up'): 'FATAL (Loss)',
        ('short_entry', 'sideways'): 'FP (Cost)',
        ('short_entry', 'down'): 'TP (Profit)',
    }

    print("\n" + "=" * 80)
    print("TRADING CONFUSION MATRIX (Time-Weighted)")
    print("=" * 80)
    print(f"Total Duration: {total_duration_ms/1000:.1f}s ({total_duration_ms/1000/60:.1f}min)")
    print()
    print("Action mapping: VERY_WEAK -> Entry signal, Others -> Hold")
    print()

    # Header
    print(f"{'Action \\ Regime':>15}", end="")
    for regime in regimes:
        header = {'up': 'Up (Long+)', 'sideways': 'Sideways', 'down': 'Down (Short+)'}
        print(f"{header[regime]:>20}", end="")
    print(f"{'Total':>12}")
    print("-" * 80)

    # Matrix rows
    for action in actions:
        action_label = {'long_entry': 'Long Entry', 'hold': 'Hold', 'short_entry': 'Short Entry'}
        print(f"{action_label[action]:>15}", end="")

        row_total = sum(matrix.get(action, {}).get(r, 0) for r in regimes)

        for regime in regimes:
            val = matrix.get(action, {}).get(regime, 0)
            pct = val / row_total * 100 if row_total > 0 else 0
            label = labels.get((action, regime), '')
            print(f"{pct:>6.1f}% {label:<12}", end="")

        print(f"{row_total/1000:>10.1f}s")

    print("-" * 80)

    # Summary metrics
    long_entry_total = sum(matrix.get('long_entry', {}).get(r, 0) for r in regimes)
    short_entry_total = sum(matrix.get('short_entry', {}).get(r, 0) for r in regimes)
    hold_total = sum(matrix.get('hold', {}).get(r, 0) for r in regimes)

    # True Positives (correct entries)
    tp_long = matrix.get('long_entry', {}).get('up', 0)
    tp_short = matrix.get('short_entry', {}).get('down', 0)
    tp_total = tp_long + tp_short

    # Fatal errors (wrong direction)
    fatal_long = matrix.get('long_entry', {}).get('down', 0)
    fatal_short = matrix.get('short_entry', {}).get('up', 0)
    fatal_total = fatal_long + fatal_short

    # False positives (entry during sideways)
    fp_long = matrix.get('long_entry', {}).get('sideways', 0)
    fp_short = matrix.get('short_entry', {}).get('sideways', 0)
    fp_total = fp_long + fp_short

    # True negatives (hold during sideways)
    tn = matrix.get('hold', {}).get('sideways', 0)

    # Missed opportunities
    missed_up = matrix.get('hold', {}).get('up', 0)
    missed_down = matrix.get('hold', {}).get('down', 0)
    missed_total = missed_up + missed_down

    entry_total = long_entry_total + short_entry_total

    print()
    print("=== TRADING METRICS ===")
    print()
    print(f"Entry Signals: {entry_total/1000:.1f}s ({entry_total/total_duration_ms*100:.1f}% of time)")
    print(f"  - Long Entry:  {long_entry_total/1000:.1f}s")
    print(f"  - Short Entry: {short_entry_total/1000:.1f}s")
    print()

    if entry_total > 0:
        print(f"Entry Quality:")
        print(f"  - True Positive (Profit):  {tp_total/1000:>8.1f}s ({tp_total/entry_total*100:>5.1f}%)")
        print(f"  - False Positive (Cost):   {fp_total/1000:>8.1f}s ({fp_total/entry_total*100:>5.1f}%)")
        print(f"  - FATAL (Wrong Direction): {fatal_total/1000:>8.1f}s ({fatal_total/entry_total*100:>5.1f}%)")
        print()

        # Win rate
        win_rate = tp_total / entry_total * 100
        fatal_rate = fatal_total / entry_total * 100
        print(f"  Win Rate: {win_rate:.1f}%")
        print(f"  Fatal Rate: {fatal_rate:.1f}%")
        print()

    print(f"Hold Period: {hold_total/1000:.1f}s ({hold_total/total_duration_ms*100:.1f}% of time)")
    print(f"  - True Negative (Sideways): {tn/1000:>8.1f}s")
    print(f"  - Missed Opportunity:       {missed_total/1000:>8.1f}s")
    print()


def print_phase_distribution(df: pd.DataFrame):
    """Print phase distribution by duration."""
    print("\n" + "=" * 70)
    print("PHASE DISTRIBUTION (by duration)")
    print("=" * 70)

    # LONG phases
    print("\nPhase LONG:")
    long_dist = df.groupby('phase_long')['duration_ms'].sum()
    total = long_dist.sum()
    for phase in ['NEUTRAL', 'BUILDING', 'DEEP', 'WEAK', 'VERY_WEAK']:
        if phase in long_dist.index:
            val = long_dist[phase]
            print(f"  {phase:>10}: {val/1000:>8.1f}s ({val/total*100:>5.1f}%)")

    # SHORT phases
    print("\nPhase SHORT:")
    short_dist = df.groupby('phase_short')['duration_ms'].sum()
    total = short_dist.sum()
    for phase in ['NEUTRAL', 'BUILDING', 'DEEP', 'WEAK', 'VERY_WEAK']:
        if phase in short_dist.index:
            val = short_dist[phase]
            print(f"  {phase:>10}: {val/1000:>8.1f}s ({val/total*100:>5.1f}%)")


def print_regime_distribution(df: pd.DataFrame):
    """Print regime distribution by duration."""
    print("\n" + "=" * 70)
    print("REGIME DISTRIBUTION (Ground Truth)")
    print("=" * 70)

    regime_dist = df.groupby('regime')['duration_ms'].sum()
    total = regime_dist.sum()
    for regime in ['up', 'down', 'sideways']:
        if regime in regime_dist.index:
            val = regime_dist[regime]
            print(f"  {regime:>10}: {val/1000:>8.1f}s ({val/total*100:>5.1f}%)")


def analyze_by_phase_intensity(df: pd.DataFrame, matrix: dict):
    """Analyze accuracy breakdown by phase intensity."""
    print("\n" + "=" * 70)
    print("ACCURACY BY PHASE INTENSITY")
    print("=" * 70)

    phases = ['NEUTRAL', 'BUILDING', 'DEEP', 'WEAK', 'VERY_WEAK']

    print("\nWhen Phase LONG is X, how often is Regime 'up'?")
    for phase in phases:
        subset = df[df['phase_long'] == phase]
        if len(subset) > 0:
            up_duration = subset[subset['regime'] == 'up']['duration_ms'].sum()
            total_duration = subset['duration_ms'].sum()
            pct = up_duration / total_duration * 100 if total_duration > 0 else 0
            print(f"  {phase:>10}: {pct:>5.1f}% up ({total_duration/1000:.1f}s total)")

    print("\nWhen Phase SHORT is X, how often is Regime 'down'?")
    for phase in phases:
        subset = df[df['phase_short'] == phase]
        if len(subset) > 0:
            down_duration = subset[subset['regime'] == 'down']['duration_ms'].sum()
            total_duration = subset['duration_ms'].sum()
            pct = down_duration / total_duration * 100 if total_duration > 0 else 0
            print(f"  {phase:>10}: {pct:>5.1f}% down ({total_duration/1000:.1f}s total)")


def main():
    parser = argparse.ArgumentParser(
        description='Analyze Phase prediction accuracy against Regime ground truth')
    parser.add_argument('parquet', help='Path to parquet file from plot_regime.py')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Show detailed breakdown')

    args = parser.parse_args()

    print(f"Loading: {args.parquet}")
    df = load_parquet(args.parquet)
    print(f"Loaded {len(df)} records")

    total_duration_ms = df['duration_ms'].sum()

    # Print distributions
    print_regime_distribution(df)
    print_phase_distribution(df)

    # Compute and print trading confusion matrix
    matrix, df = compute_trading_matrix(df)
    print_trading_matrix(matrix, total_duration_ms)

    # Detailed analysis
    if args.verbose:
        analyze_by_phase_intensity(df, matrix)

    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""
Analyze Phase prediction accuracy against Regime ground truth.

Mean Reversion Entry Confusion Matrix:
- Rows: Entry action (Long Entry, Short Entry)
- Cols: Actual regime at entry (Up=Rebound, Sideways=Range, Down=Trend)

Mean Reversion perspective:
- Long Entry + Up (Rebound): TP - caught the bounce
- Long Entry + Sideways: Marginal - breakeven to small profit (MR works in range)
- Long Entry + Down (Trend): FATAL - caught falling knife (adverse selection)
- Short Entry + Up: FATAL - shorted a rocket
- Short Entry + Sideways: Marginal - breakeven to small profit
- Short Entry + Down: TP - sold the top

Phase to Action mapping (from mean_reversion_maker.h):
- SHORT VERY_WEAK -> Long Entry (oversold bounce expected)
- LONG VERY_WEAK -> Short Entry (overbought drop expected)
- Otherwise -> Hold
"""

import argparse
import sys
from collections import defaultdict

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


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

    Logic (from mean_reversion_maker.h):
    - SHORT VERY_WEAK -> 'long_entry' (uptrend momentum weakening -> expect continuation up)
    - LONG VERY_WEAK -> 'short_entry' (downtrend momentum weakening -> expect continuation down)
    - Both VERY_WEAK -> ambiguous, default to hold
    - Otherwise -> 'hold' (no entry signal yet)
    """
    phase_long = row['phase_long']
    phase_short = row['phase_short']

    # Note: Logic is OPPOSITE of what you might expect
    # SHORT VERY_WEAK triggers long entry, LONG VERY_WEAK triggers short entry
    long_entry = phase_short == 'VERY_WEAK'   # SHORT weak -> go long
    short_entry = phase_long == 'VERY_WEAK'   # LONG weak -> go short

    if long_entry and not short_entry:
        return 'long_entry'
    elif short_entry and not long_entry:
        return 'short_entry'
    elif long_entry and short_entry:
        # Both VERY_WEAK - ambiguous, default to hold
        return 'hold'
    else:
        return 'hold'


def compute_entry_matrix(df: pd.DataFrame) -> tuple[dict, pd.DataFrame]:
    """
    Compute entry count confusion matrix.

    Returns:
    - count_matrix[action][regime] = entry_count
    - df with action column added
    """
    # Add action column
    df['action'] = df.apply(map_phase_to_action, axis=1)

    regimes = ['up', 'sideways', 'down']
    count_matrix = defaultdict(lambda: defaultdict(int))

    # Track action transitions for counting entries
    prev_action = None

    # Count entries (transitions INTO entry signals)
    for _, row in df.iterrows():
        action = row['action']
        regime = row['regime']

        # Count entry when action changes to entry signal
        if action != prev_action and action in ['long_entry', 'short_entry']:
            count_matrix[action][regime] += 1
        prev_action = action

    return dict(count_matrix), df


def print_entry_matrix(count_matrix: dict):
    """Print entry count confusion matrix."""
    actions = ['long_entry', 'short_entry']
    regimes = ['up', 'sideways', 'down']

    labels = {
        ('long_entry', 'up'): 'TP (Rebound)',
        ('long_entry', 'sideways'): 'Marginal',
        ('long_entry', 'down'): 'FATAL (Knife)',
        ('short_entry', 'up'): 'FATAL (Rocket)',
        ('short_entry', 'sideways'): 'Marginal',
        ('short_entry', 'down'): 'TP (Top)',
    }

    print("\n" + "=" * 80)
    print("ENTRY CONFUSION MATRIX (Number of Entries)")
    print("=" * 80)
    print()
    print("Counts transitions INTO entry signals (action changed to long/short_entry)")
    print("Action: SHORT VERY_WEAK -> Long, LONG VERY_WEAK -> Short")
    print()

    # Header
    header_label = "Action \\ Regime"
    print(f"{header_label:>15}", end="")
    for regime in regimes:
        header = {'up': 'Up (Rebound)', 'sideways': 'Sideways (Range)', 'down': 'Down (Trend)'}
        print(f"{header[regime]:>20}", end="")
    print(f"{'Total':>12}")
    print("-" * 80)

    # Matrix rows
    total_entries = 0
    for action in actions:
        action_label = {'long_entry': 'Long Entry', 'short_entry': 'Short Entry'}
        print(f"{action_label[action]:>15}", end="")

        row_total = sum(count_matrix.get(action, {}).get(r, 0) for r in regimes)
        total_entries += row_total

        for regime in regimes:
            val = count_matrix.get(action, {}).get(regime, 0)
            pct = val / row_total * 100 if row_total > 0 else 0
            label = labels.get((action, regime), '')
            print(f"{pct:>5.1f}% ({val:>4}) {label:<12}", end="")

        print(f"{row_total:>10}")

    print("-" * 80)

    # Summary
    tp_long = count_matrix.get('long_entry', {}).get('up', 0)
    tp_short = count_matrix.get('short_entry', {}).get('down', 0)
    tp_total = tp_long + tp_short

    fatal_long = count_matrix.get('long_entry', {}).get('down', 0)
    fatal_short = count_matrix.get('short_entry', {}).get('up', 0)
    fatal_total = fatal_long + fatal_short

    marginal_long = count_matrix.get('long_entry', {}).get('sideways', 0)
    marginal_short = count_matrix.get('short_entry', {}).get('sideways', 0)
    marginal_total = marginal_long + marginal_short

    print()
    print(f"Total Entries: {total_entries}")
    if total_entries > 0:
        win_rate = tp_total / total_entries * 100
        marginal_rate = marginal_total / total_entries * 100
        fatal_rate = fatal_total / total_entries * 100

        print(f"  - TP (Rebound/Top):  {tp_total:>4} ({win_rate:>5.1f}%)")
        print(f"  - Marginal (Range):  {marginal_total:>4} ({marginal_rate:>5.1f}%)")
        print(f"  - FATAL (Trend):     {fatal_total:>4} ({fatal_rate:>5.1f}%)")
        print()
        print(f"  Win Rate (TP only): {win_rate:.1f}%")
        print(f"  Safe Rate (TP+Marginal): {win_rate + marginal_rate:.1f}%")
        print(f"  Fatal Rate: {fatal_rate:.1f}%")
    print()


def plot_entry_matrix(count_matrix: dict, output_path: str):
    """Plot entry count confusion matrix as heatmap."""
    actions = ['long_entry', 'short_entry']
    regimes = ['up', 'sideways', 'down']

    # Build matrix data (percentages within each row)
    data = np.zeros((2, 3))
    counts = np.zeros((2, 3))
    for i, action in enumerate(actions):
        row_total = sum(count_matrix.get(action, {}).get(r, 0) for r in regimes)
        for j, regime in enumerate(regimes):
            val = count_matrix.get(action, {}).get(regime, 0)
            data[i, j] = val / row_total * 100 if row_total > 0 else 0
            counts[i, j] = val

    # Color scheme
    cell_colors = np.array([
        ['#90EE90', '#FFD700', '#FF6B6B'],  # Long Entry: TP(Rebound), Marginal, FATAL(Knife)
        ['#FF6B6B', '#FFD700', '#90EE90'],  # Short Entry: FATAL(Rocket), Marginal, TP(Top)
    ])

    cell_labels = np.array([
        ['TP\n(Rebound)', 'Marginal\n(Range)', 'FATAL\n(Knife)'],
        ['FATAL\n(Rocket)', 'Marginal\n(Range)', 'TP\n(Top)'],
    ])

    fig, ax = plt.subplots(figsize=(10, 6))

    # Draw cells with custom colors
    for i in range(2):
        for j in range(3):
            color = cell_colors[i, j]
            rect = plt.Rectangle((j, 1-i), 1, 1, facecolor=color, edgecolor='white', linewidth=2)
            ax.add_patch(rect)

            # Add percentage, count, and label
            pct = data[i, j]
            cnt = int(counts[i, j])
            label = cell_labels[i, j]
            ax.text(j + 0.5, 1-i + 0.70, f'{pct:.1f}%', ha='center', va='center',
                   fontsize=16, fontweight='bold')
            ax.text(j + 0.5, 1-i + 0.50, f'({cnt})', ha='center', va='center',
                   fontsize=12, color='#555555')
            ax.text(j + 0.5, 1-i + 0.28, label, ha='center', va='center',
                   fontsize=10, color='#333333')

    # Labels
    action_labels = ['Long Entry', 'Short Entry']
    regime_labels = ['Up\n(Rebound)', 'Sideways\n(Range)', 'Down\n(Trend)']

    total_entries = int(counts.sum())

    ax.set_xlim(0, 3)
    ax.set_ylim(0, 2)
    ax.set_xticks([0.5, 1.5, 2.5])
    ax.set_xticklabels(regime_labels, fontsize=11)
    ax.set_yticks([0.5, 1.5])
    ax.set_yticklabels(reversed(action_labels), fontsize=11)

    ax.set_xlabel('Regime at Entry (Ground Truth)', fontsize=12, fontweight='bold')
    ax.set_ylabel('Entry Action (Phase Signal)', fontsize=12, fontweight='bold')
    ax.set_title(f'Mean Reversion Entry Confusion Matrix\n(Total: {total_entries} entries)',
                fontsize=14, fontweight='bold')

    # Remove spines
    for spine in ax.spines.values():
        spine.set_visible(False)

    ax.set_aspect('equal')

    # Add legend
    legend_elements = [
        plt.Rectangle((0, 0), 1, 1, facecolor='#90EE90', label='Good (TP)'),
        plt.Rectangle((0, 0), 1, 1, facecolor='#FFD700', label='Marginal (Range)'),
        plt.Rectangle((0, 0), 1, 1, facecolor='#FF6B6B', label='Fatal (Adverse)'),
    ]
    ax.legend(handles=legend_elements, loc='upper left', bbox_to_anchor=(1.02, 1))

    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved confusion matrix plot to {output_path}")


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


def analyze_by_phase_intensity(df: pd.DataFrame, count_matrix: dict):
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
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='Output image file path for confusion matrix plot')

    args = parser.parse_args()

    print(f"Loading: {args.parquet}")
    df = load_parquet(args.parquet)
    print(f"Loaded {len(df)} records")

    # Print distributions
    print_regime_distribution(df)
    print_phase_distribution(df)

    # Compute and print entry confusion matrix
    count_matrix, df = compute_entry_matrix(df)
    print_entry_matrix(count_matrix)

    # Plot confusion matrix
    if args.output:
        plot_entry_matrix(count_matrix, args.output)

    # Detailed analysis
    if args.verbose:
        analyze_by_phase_intensity(df, count_matrix)

    return 0


if __name__ == '__main__':
    sys.exit(main())

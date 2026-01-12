#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_SCRIPT="$SCRIPT_DIR/manage_cpu_isolation.sh"
ANALYZE_SCRIPT="$SCRIPT_DIR/analyze_rdtsc.py"
PLOT_REGIME_SCRIPT="$SCRIPT_DIR/plot_regime.py"
PHASE_ACCURACY_SCRIPT="$SCRIPT_DIR/analyze_phase_accuracy.py"

PERF_CMD=$(find /usr/lib/linux-tools-*/perf 2>/dev/null | head -1)
HUGETLB_LIB=$(find /usr/lib/*/libhugetlbfs.so* 2>/dev/null | head -1)
PERF_MODE="stat"
RECORD_EVENTS="iTLB-load-misses,L1-icache-load-misses"
RECORD_CPU=""

NUMA_NODE=""
NUMA_OPTS=""
if [ -d /sys/devices/system/node/node1 ]; then
    NUMA_NODE="0"
    NUMA_OPTS="-p NUMAPolicy=bind -p NUMAMask=$NUMA_NODE"
    echo ">>> NUMA detected, will bind to node $NUMA_NODE"
fi

if lscpu | grep -q "GenuineIntel"; then
    PERF_EVENTS="cycles,instructions,branches,branch-misses,context-switches,page-faults,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,L1-icache-load-misses,dTLB-loads,dTLB-load-misses,iTLB-loads,iTLB-load-misses"
else
    PERF_EVENTS="cycles,instructions,branches,branch-misses,context-switches,page-faults,l1_data_cache_fills_all,l1_data_cache_fills_from_within_same_ccx,l1_data_cache_fills_from_memory,l2_cache_hits_from_dc_misses,l2_cache_misses_from_dc_misses,l1_dtlb_misses,l2_dtlb_misses,bp_l1_tlb_miss_l2_tlb_hit,bp_l1_tlb_miss_l2_tlb_miss,ic_tag_hit_miss.instruction_cache_miss,ic_fetch_stall.ic_stall_any"
fi
DEFAULT_BUILD_DIR_A="cmake-build-relwithdebinfo/test"
#DEFAULT_BUILD_DIR_A="cmake-build-measure/test"
DEFAULT_BUILD_DIR_B="cmake-build-relwithdebinfo-clang-double/test"
DEFAULT_PROGRAM="full_pipeline_benchmark_mean_reversion_tests"

print_usage() {
    echo "Usage: sudo $0 [OPTIONS] [BUILD_DIR_A] [BUILD_DIR_B] [PROGRAM] [args...]"
    echo ""
    echo "Options:"
    echo "  --record       Use perf record instead of perf stat (for detailed analysis)"
    echo "  --cpu <N>      Record only specific CPU (e.g., --cpu 2 for TradeEngine)"
    echo "  -h, --help     Show this help"
    echo ""
    echo "Example: sudo $0"
    echo "         sudo $0 --record --cpu 2 cmake-build-a cmake-build-b"
    echo "Defaults:"
    echo "  BUILD_DIR_A: $DEFAULT_BUILD_DIR_A"
    echo "  BUILD_DIR_B: $DEFAULT_BUILD_DIR_B"
    echo "  PROGRAM: $DEFAULT_PROGRAM"
    echo "  BRANCH_PROFILE: 1 (enabled)"
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage
    exit 0
fi

while [[ "$1" == --* ]]; do
    case "$1" in
        --record)
            PERF_MODE="record"
            shift
            ;;
        --cpu)
            RECORD_CPU="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

if [ "$EUID" -ne 0 ]; then
    echo "Error: root 권한(sudo)으로 실행해주세요."
    exit 1
fi

BUILD_DIR_A="${1:-$DEFAULT_BUILD_DIR_A}"
BUILD_DIR_B="${2:-$DEFAULT_BUILD_DIR_B}"

if [ "$#" -ge 2 ]; then
    shift 2
    if [ "$#" -ge 1 ]; then
        PROGRAM="$1"
        shift
    else
        PROGRAM="$DEFAULT_PROGRAM"
    fi
else
    shift $#
    PROGRAM="$DEFAULT_PROGRAM"
fi

build_program() {
    local build_dir="$1"
    local cmake_build_dir="${build_dir%/test}"

    echo ">>> Building $PROGRAM in $cmake_build_dir..."
    # Run build as original user to avoid permission issues
    if [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" cmake --build "$cmake_build_dir" --target "$PROGRAM" -j
    else
        cmake --build "$cmake_build_dir" --target "$PROGRAM" -j
    fi
}

check_iso_slice_available() {
    if ! command -v systemd-run &> /dev/null; then
        return 1
    fi
    if ! systemctl list-units --type=slice 2>/dev/null | grep -q "iso.slice"; then
        if [ ! -f /sys/fs/cgroup/iso.slice/cgroup.controllers ] 2>/dev/null; then
            return 1
        fi
    fi
    return 0
}

run_with_perf() {
    local workdir="$1"
    shift
    local cpu_opt=""
    if [ -n "$RECORD_CPU" ]; then
        cpu_opt="--cpu $RECORD_CPU"
    fi
    if [ "$PERF_MODE" = "record" ]; then
        if [ -n "$RECORD_CPU" ]; then
            echo ">>> Running with perf record (events: $RECORD_EVENTS, CPU: $RECORD_CPU)..."
        else
            echo ">>> Running with perf record (events: $RECORD_EVENTS)..."
        fi
        echo ">>> Output: $perf_output"
        $PERF_CMD record -e "$RECORD_EVENTS" $cpu_opt -g --call-graph dwarf -o "$perf_output" "$@"
        echo ""
        echo ">>> Perf report (top 50):"
        $PERF_CMD report -i "$perf_output" --stdio --sort=dso,symbol | head -70
    else
        if [ -n "$RECORD_CPU" ]; then
            echo ">>> Running with perf stat (CPU: $RECORD_CPU)..."
        else
            echo ">>> Running with perf stat..."
        fi
        $PERF_CMD stat -e "$PERF_EVENTS" $cpu_opt "$@"
    fi
}

run_test() {
    local build_dir="$(cd "$1" && pwd)"
    local label="$2"
    shift 2
    local program_path="$build_dir/$PROGRAM"

    if [ ! -x "$program_path" ]; then
        echo "Error: $program_path not found or not executable"
        return 1
    fi

    echo ""
    echo "============================================================"
    echo " [$label] Running: $program_path"
    echo "============================================================"

    export perf_output="$build_dir/perf_$(date +%Y%m%d_%H%M%S).data"

    if check_iso_slice_available && [ -x "$ISO_SCRIPT" ]; then
        echo ">>> Starting CPU isolation..."
        sudo "$ISO_SCRIPT" start

        if [ -n "$NUMA_NODE" ]; then
            echo ">>> Using NUMA node $NUMA_NODE"
        fi

        local env_opts=""
        if [ -n "$HUGETLB_LIB" ]; then
            echo ">>> Using libhugetlbfs for code huge pages..."
            env_opts="-E LD_PRELOAD=$HUGETLB_LIB -E HUGETLB_ELFMAP=RW -E HUGETLB_DEBUG=1"
        fi

        local perf_opts=""
        if [ -n "$RECORD_CPU" ]; then
            perf_opts="--cpu $RECORD_CPU"
        fi

        if [ "$PERF_MODE" = "record" ]; then
            echo ">>> Running with perf record (events: $RECORD_EVENTS)..."
            sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 $NUMA_OPTS \
                --working-directory="$build_dir" $env_opts \
                $PERF_CMD record -e "$RECORD_EVENTS" $perf_opts -g --call-graph dwarf -o "$perf_output" "$program_path" "$@"
            echo ">>> Perf report (top 50):"
            $PERF_CMD report -i "$perf_output" --stdio --sort=dso,symbol | head -70
        else
            echo ">>> Running with perf stat..."
            sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 $NUMA_OPTS \
                --working-directory="$build_dir" $env_opts \
                $PERF_CMD stat -e "$PERF_EVENTS" $perf_opts "$program_path" "$@"
        fi

        echo ">>> Stopping CPU isolation..."
        sudo "$ISO_SCRIPT" stop
    else
        echo ">>> iso.slice not available, running without CPU isolation..."
        pushd "$build_dir" > /dev/null

        if [ -n "$PERF_CMD" ]; then
            if [ -n "$HUGETLB_LIB" ]; then
                echo ">>> Using libhugetlbfs for code huge pages..."
                LD_PRELOAD="$HUGETLB_LIB" HUGETLB_ELFMAP=RW HUGETLB_DEBUG=1 \
                    run_with_perf "$build_dir" "$program_path" "$@" 2>&1 || "$program_path" "$@"
            else
                run_with_perf "$build_dir" "$program_path" "$@" 2>&1 || "$program_path" "$@"
            fi
        else
            "$program_path" "$@"
        fi
        popd > /dev/null
    fi

    echo ">>> Analyzing RDTSC..."
    sudo -u "$SUDO_USER" python3 "$ANALYZE_SCRIPT" -d "$build_dir" -k benchmark_rdtsc -m || true

    # Find latest benchmark log for regime/phase analysis (pass base name without .log)
    local latest_log=$(ls -t "$build_dir"/benchmark_rdtsc_*.log 2>/dev/null | head -1)
    local log_base="${latest_log%.log}"  # Remove .log extension
    # Remove only short _N suffix (1-2 digits), not timestamps
    log_base=$(echo "$log_base" | sed 's/_[0-9]\{1,2\}$//')
    if [ -n "$latest_log" ] && [ -f "$PLOT_REGIME_SCRIPT" ]; then
        local timestamp=$(date +%Y%m%d_%H%M%S)
        local output_dir="$build_dir/analysis_$timestamp"
        sudo -u "$SUDO_USER" mkdir -p "$output_dir"

        echo ">>> Plotting regime & phase..."
        sudo -u "$SUDO_USER" python3 "$PLOT_REGIME_SCRIPT" "$log_base" \
            --stats \
            --output "$output_dir/regime_phase.png" \
            --parquet "$output_dir/regime_phase_data.parquet" --slice-ms 30000 --threshold-bps 5.0 || true

        if [ -f "$output_dir/regime_phase_data.parquet" ] && [ -f "$PHASE_ACCURACY_SCRIPT" ]; then
            echo ">>> Analyzing phase accuracy..."
            sudo -u "$SUDO_USER" python3 "$PHASE_ACCURACY_SCRIPT" "$output_dir/regime_phase_data.parquet" \
                -v \
                -o "$output_dir/confusion_matrix.png" || true
        fi

        echo ">>> Analysis outputs saved to: $output_dir"
    fi

    echo ""
    echo "[$label] Done."
}

build_program "$BUILD_DIR_A"
#build_program "$BUILD_DIR_B"

run_test "$BUILD_DIR_A" "A" "$@"
#run_test "$BUILD_DIR_B" "B" "$@"

echo ""
echo "============================================================"
echo " AB Test Complete"
echo "============================================================"

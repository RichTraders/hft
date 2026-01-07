#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_SCRIPT="$SCRIPT_DIR/manage_cpu_isolation.sh"
ANALYZE_SCRIPT="$SCRIPT_DIR/analyze_rdtsc.py"

PERF_EVENTS="cycles,instructions,branches,branch-misses,context-switches,page-faults,l1_data_cache_fills_all,l1_data_cache_fills_from_within_same_ccx,l1_data_cache_fills_from_memory,l2_cache_hits_from_dc_misses,l2_cache_misses_from_dc_misses,l1_dtlb_misses,l2_dtlb_misses"
DEFAULT_BUILD_DIR_A="cmake-build-relwithdebinfo-clang/test"
DEFAULT_BUILD_DIR_B="cmake-build-relwithdebinfo-clang-double/test"
DEFAULT_PROGRAM="full_pipeline_benchmark_mean_reversion_tests"

print_usage() {
    echo "Usage: sudo $0 [BUILD_DIR_A] [BUILD_DIR_B] [PROGRAM] [args...]"
    echo "Example: sudo $0"
    echo "         sudo $0 cmake-build-a cmake-build-b"
    echo "Defaults:"
    echo "  BUILD_DIR_A: $DEFAULT_BUILD_DIR_A"
    echo "  BUILD_DIR_B: $DEFAULT_BUILD_DIR_B"
    echo "  PROGRAM: $DEFAULT_PROGRAM"
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    print_usage
    exit 0
fi

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
    cmake --build "$cmake_build_dir" --target "$PROGRAM" -j
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

    echo ">>> Starting CPU isolation..."
    sudo "$ISO_SCRIPT" start

    echo ">>> Running with perf stat..."
    echo "asdfasd : $build_dir"
    sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 \
        --working-directory="$build_dir" \
        /usr/lib/linux-tools-6.8.0-90/perf stat -e "$PERF_EVENTS" "$program_path" "$@"

    echo ">>> Stopping CPU isolation..."
    sudo "$ISO_SCRIPT" stop

    echo ">>> Analyzing RDTSC..."
    python3 "$ANALYZE_SCRIPT" -d "$build_dir" -k benchmark_rdtsc -m

    echo ""
    echo "[$label] Done."
}

build_program "$BUILD_DIR_A"
build_program "$BUILD_DIR_B"

run_test "$BUILD_DIR_A" "A" "$@"
#run_test "$BUILD_DIR_B" "B" "$@"

echo ""
echo "============================================================"
echo " AB Test Complete"
echo "============================================================"

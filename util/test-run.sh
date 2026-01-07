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
    # Run build as original user to avoid permission issues
    if [ -n "$SUDO_USER" ]; then
        sudo -u "$SUDO_USER" cmake --build "$cmake_build_dir" --target "$PROGRAM" -j
    else
        cmake --build "$cmake_build_dir" --target "$PROGRAM" -j
    fi
}

check_iso_slice_available() {
    # Check if systemd-run and iso.slice are available
    if ! command -v systemd-run &> /dev/null; then
        return 1
    fi
    if ! systemctl list-units --type=slice 2>/dev/null | grep -q "iso.slice"; then
        # Try to check if iso.slice can be created
        if [ ! -f /sys/fs/cgroup/iso.slice/cgroup.controllers ] 2>/dev/null; then
            return 1
        fi
    fi
    return 0
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

    if check_iso_slice_available && [ -x "$ISO_SCRIPT" ]; then
        echo ">>> Starting CPU isolation..."
        sudo "$ISO_SCRIPT" start

        echo ">>> Running with perf stat (iso.slice)..."
        sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 \
            --working-directory="$build_dir" \
            /usr/lib/linux-tools-6.8.0-90/perf stat -e "$PERF_EVENTS" "$program_path" "$@"

        echo ">>> Stopping CPU isolation..."
        sudo "$ISO_SCRIPT" stop
    else
        echo ">>> iso.slice not available, running without CPU isolation..."
        pushd "$build_dir" > /dev/null
        if command -v perf &> /dev/null; then
            perf stat -e "$PERF_EVENTS" "$program_path" "$@" 2>&1 || "$program_path" "$@"
        else
            "$program_path" "$@"
        fi
        popd > /dev/null
    fi

    echo ">>> Analyzing RDTSC..."
    python3 "$ANALYZE_SCRIPT" -d "$build_dir" -k benchmark_rdtsc -m

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

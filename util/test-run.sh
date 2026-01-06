#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_SCRIPT="$SCRIPT_DIR/manage_cpu_isolation.sh"
ANALYZE_SCRIPT="$SCRIPT_DIR/analyze_rdtsc.py"

PERF_EVENTS="cycles,instructions,branches,branch-misses,context-switches,page-faults,l1_data_cache_fills_all,l1_data_cache_fills_from_within_same_ccx,l1_data_cache_fills_from_memory,l2_cache_hits_from_dc_misses,l2_cache_misses_from_dc_misses,l1_dtlb_misses,l2_dtlb_misses"

BRANCH_PROFILE="${BRANCH_PROFILE:-1}"

find_perf() {
    local kernel_ver
    kernel_ver="$(uname -r)"

    if [ -x "/usr/lib/linux-tools/$kernel_ver/perf" ]; then
        echo "/usr/lib/linux-tools/$kernel_ver/perf"
        return
    fi
    for p in /usr/lib/linux-tools/*/perf /usr/lib/linux-tools-*/perf; do
        if [ -x "$p" ]; then
            echo "$p"
            return
        fi
    done

    if command -v perf &>/dev/null && perf --version &>/dev/null; then
        command -v perf
        return
    fi
}

PERF_CMD="$(find_perf)"
if [ -n "$PERF_CMD" ]; then
    echo "Found perf: $PERF_CMD"
else
    echo "Warning: perf not found. Running without perf stat."
    echo "  Install with: sudo apt install linux-tools-generic linux-tools-\$(uname -r)"
fi

DEFAULT_BUILD_DIR_A="cmake-build-relwithdebinfo-clang/test"
DEFAULT_BUILD_DIR_B="cmake-build-relwithdebinfo-clang-double/test"
DEFAULT_PROGRAM="full_pipeline_benchmark_tests"

print_usage() {
    echo "Usage: sudo $0 [BUILD_DIR_A] [BUILD_DIR_B] [PROGRAM] [args...]"
    echo "       sudo BRANCH_PROFILE=0 $0 ...  # branch-misses 프로파일링 비활성화"
    echo "Example: sudo $0"
    echo "         sudo $0 cmake-build-a cmake-build-b"
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

    if [ -n "$PERF_CMD" ]; then
        #echo ">>> Running with perf stat ($PERF_CMD)..."
        #sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 \
        #    --working-directory="$build_dir" \
        #    "$PERF_CMD" stat -e "$PERF_EVENTS" "$program_path" "$@"

        if [ "$BRANCH_PROFILE" = "1" ]; then
            local perf_data="$build_dir/perf_${label}.data"
            echo ">>> Recording cache-misses profile (CPU 1-4 only, excluding logger)..."
            sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 \
                --working-directory="$build_dir" \
                "$PERF_CMD" record -e cache-misses -g -C 1-4 -o "$perf_data" "$program_path" "$@"
            chmod 644 "$perf_data"
            echo ">>> Cache-misses hotspots:"
            "$PERF_CMD" report -i "$perf_data" --stdio --no-children -n --percent-limit=0.1 --dso=full_pipeline_benchmark_tests
        fi
    else
        echo ">>> Running without perf..."
        sudo systemd-run --scope --slice=iso.slice -p AllowedCPUs=1-5 \
            --working-directory="$build_dir" \
            "$program_path" "$@"
    fi

    echo ">>> Stopping CPU isolation..."
    sudo "$ISO_SCRIPT" stop

    echo ">>> Analyzing RDTSC..."
    python3 "$ANALYZE_SCRIPT" -d "$build_dir" -k benchmark_rdtsc -m

    echo ""
    echo "[$label] Done."
}

#run_test "$BUILD_DIR_A" "A" "$@"
run_test "$BUILD_DIR_B" "B" "$@"

echo ""
echo "============================================================"
echo " AB Test Complete"
echo "============================================================"

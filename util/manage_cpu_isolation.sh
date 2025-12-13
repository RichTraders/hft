#!/bin/bash
# CPU Isolation Management Script for HFT Service
#
# This script dynamically manages CPU allocation for cgroup slices:
# - When HFT starts: Restricts other slices to use only non-isolated CPUs
# - When HFT stops: Restores all slices to use all available CPUs
#
# Usage:
#   manage_cpu_isolation.sh start   # Restrict other slices
#   manage_cpu_isolation.sh stop    # Restore all slices

set -e

# Configuration
ISO_SLICE_FILE="/etc/systemd/system/iso.slice"
CGROUP_ROOT="/sys/fs/cgroup"

# Slices to manage (excluding iso.slice)
SLICES=("init.scope" "system.slice" "user.slice" "machine.slice")

# Log function
log() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] $*" >&2
}

get_total_cpus() {
    local present
    present=$(cat /sys/devices/system/cpu/present)

    if [[ $present =~ ^([0-9]+)-([0-9]+)$ ]]; then
        # Range format: N-M, return M+1
        echo $((${BASH_REMATCH[2]} + 1))
    elif [[ $present =~ ^[0-9]+$ ]]; then
        # Single CPU: just return 1
        echo "1"
    else
        log "ERROR: Cannot parse CPU present format: $present"
        exit 1
    fi
}

parse_cpu_range() {
    local range="$1"
    local cpus=()

    # Handle comma-separated ranges (e.g., "0-4,8-11")
    IFS=',' read -ra PARTS <<< "$range"
    for part in "${PARTS[@]}"; do
        if [[ $part =~ ^([0-9]+)-([0-9]+)$ ]]; then
            # Range format: N-M
            local start=${BASH_REMATCH[1]}
            local end=${BASH_REMATCH[2]}
            for ((i=start; i<=end; i++)); do
                cpus+=("$i")
            done
        elif [[ $part =~ ^[0-9]+$ ]]; then
            cpus+=("$part")
        fi
    done

    echo "${cpus[@]}"
}

get_iso_cpus() {
    if [[ ! -f "$ISO_SLICE_FILE" ]]; then
        log "ERROR: iso.slice file not found: $ISO_SLICE_FILE"
        exit 1
    fi

    local allowed_cpus
    allowed_cpus=$(grep -E '^\s*AllowedCPUs=' "$ISO_SLICE_FILE" | cut -d= -f2 | tr -d ' ')

    if [[ -z "$allowed_cpus" ]]; then
        log "ERROR: AllowedCPUs not found in $ISO_SLICE_FILE"
        exit 1
    fi

    echo "$allowed_cpus"
}

calculate_remaining_cpus() {
    local total_cpus
    total_cpus=$(get_total_cpus)

    local iso_cpu_range
    iso_cpu_range=$(get_iso_cpus)

    local iso_cpus
    read -ra iso_cpus <<< "$(parse_cpu_range "$iso_cpu_range")"

    local all_cpus=()
    for ((i=0; i<total_cpus; i++)); do
        all_cpus+=("$i")
    done

    # Remove iso CPUs from all CPUs
    local remaining_cpus=()
    for cpu in "${all_cpus[@]}"; do
        local found=0
        for iso_cpu in "${iso_cpus[@]}"; do
            if [[ $cpu -eq $iso_cpu ]]; then
                found=1
                break
            fi
        done
        if [[ $found -eq 0 ]]; then
            remaining_cpus+=("$cpu")
        fi
    done

    # Convert array to range string
    if [[ ${#remaining_cpus[@]} -eq 0 ]]; then
        log "ERROR: No remaining CPUs after iso.slice allocation"
        exit 1
    fi

    # Simple implementation: output as comma-separated list
    # A more sophisticated version could convert to ranges (e.g., "5-11")
    local result="${remaining_cpus[0]}"
    for ((i=1; i<${#remaining_cpus[@]}; i++)); do
        local prev=${remaining_cpus[$((i-1))]}
        local curr=${remaining_cpus[$i]}
        if [[ $((prev + 1)) -eq $curr ]]; then
            if [[ ! $result =~ -$ ]]; then
                result="${result}-"
            fi
        else
            if [[ $result =~ -$ ]]; then
                result="${result}${prev},"
            else
                result="${result},"
            fi
            result="${result}${curr}"
        fi
    done

    if [[ $result =~ -$ ]]; then
        result="${result}${remaining_cpus[-1]}"
    fi

    echo "$result"
}

set_slice_cpus() {
    local slice="$1"
    local cpus="$2"
    local cgroup_path="$CGROUP_ROOT/$slice"

    if [[ ! -d "$cgroup_path" ]]; then
        log "WARNING: Slice $slice does not exist, skipping"
        return 0
    fi

    local cpuset_file="$cgroup_path/cpuset.cpus"
    if [[ ! -f "$cpuset_file" ]]; then
        log "WARNING: cpuset.cpus not found for $slice, skipping"
        return 0
    fi

    systemctl set-property "$slice" AllowedCPUs="$cpus" || {
        log "ERROR: Failed to set CPUs for $slice via systemctl"
        return 1
    }

    log "Set $slice CPUs to: $cpus"
    return 0
}

do_start() {
    log "Starting CPU isolation for HFT service"

    local iso_cpus
    iso_cpus=$(get_iso_cpus)

    local remaining_cpus
    remaining_cpus=$(calculate_remaining_cpus)

    log "iso.slice CPUs: $iso_cpus"
    log "Other slices CPUs: $remaining_cpus"

    # Set iso.slice to use only isolated CPUs
    # This overrides any previous runtime settings from stop
    systemctl set-property iso.slice AllowedCPUs="$iso_cpus" || {
        log "WARNING: Failed to set iso.slice AllowedCPUs"
    }

    for slice in "${SLICES[@]}"; do
        set_slice_cpus "$slice" "$remaining_cpus"
    done

    log "CPU isolation applied successfully"
}

do_stop() {
    log "Restoring CPU allocation after HFT service stop"

    local total_cpus
    total_cpus=$(get_total_cpus)
    local all_cpus="0-$((total_cpus - 1))"

    log "Restoring all slices to: $all_cpus"

    # Restore iso.slice AllowedCPUs using systemctl
    # This ensures the restriction is removed from cgroup
    systemctl set-property iso.slice AllowedCPUs="$all_cpus" || {
        log "WARNING: Failed to restore iso.slice AllowedCPUs"
    }

    for slice in "${SLICES[@]}"; do
        set_slice_cpus "$slice" "$all_cpus"
    done

    log "CPU allocation restored successfully"
}

main() {
    if [[ $# -ne 1 ]]; then
        echo "Usage: $0 {start|stop}" >&2
        exit 1
    fi

    case "$1" in
        start)
            do_start
            ;;
        stop)
            do_stop
            ;;
        *)
            echo "Invalid argument: $1" >&2
            echo "Usage: $0 {start|stop}" >&2
            exit 1
            ;;
    esac
}

main "$@"

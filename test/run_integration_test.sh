#!/bin/bash
# Wrapper script to run integration tests with systemd-run

TEST_BINARY="$1"
shift

if grep -q "iso.slice" /proc/self/cgroup 2>/dev/null; then
    exec "$TEST_BINARY" "$@"
else
    echo "Running in iso.slice with systemd-run..."
    sudo systemd-run --scope --slice=iso.slice \
        -p AllowedCPUs=0-4 \
        "$TEST_BINARY" "$@"
fi

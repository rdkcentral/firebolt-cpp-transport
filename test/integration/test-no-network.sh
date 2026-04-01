#!/usr/bin/env bash
# Integration test: ConnectViaNumericLoopbackIP with no external network.
#
# Reproduces the RDK STB failure (RDKEMW-16441):
#   websocketpp used AI_ADDRCONFIG by default; on boxes with no active eth0/wifi,
#   getaddrinfo("127.0.0.1") returned HOST_NOT_FOUND even though loopback is up.
#
# Isolation strategy (tried in order):
#   1. Docker --network none  — loopback-only container; no special kernel privileges needed.
#                               Uses the same CI image as ./build.sh --docker.
#                               This is the preferred method and the pattern for future
#                               network-isolation tests in this repo.
#   2. unshare --net          — kernel user namespace; requires
#                               sysctl kernel.unprivileged_userns_clone=1 (often restricted).
#
# Usage:
#   ./test/integration/test-no-network.sh [path-to-utApp]
#
# The test binary defaults to build-dev/test/utApp (built with +tests).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BINARY="${1:-$REPO_ROOT/build-dev/test/utApp}"
IMAGE="firebolt-cpp-transport-ci:local"
FILTER="TransportNumericIPUTest.*:TransportIPv6UTest.ConnectViaIPv6LoopbackIP:TransportNumericIPResolverTest.ConnectFailureViaNumericIP"

if [[ ! -x "$BINARY" ]]; then
    echo "error: test binary not found: $BINARY" >&2
    echo "  Build first: ./build.sh +tests  (or ./build.sh --docker +tests)" >&2
    exit 1
fi

# Path of the binary relative to REPO_ROOT (used for Docker volume mount)
REL_BIN="${BINARY#"$REPO_ROOT"/}"

run_in_docker() {
    if ! docker image inspect "$IMAGE" &>/dev/null; then
        echo "Building CI Docker image (one-time)..."
        docker build -t "$IMAGE" -f "$REPO_ROOT/.github/Dockerfile" "$REPO_ROOT"
    fi
    echo "==> Testing in Docker --network none: $BINARY"
    docker run --rm --network none \
        --user "$(id -u):$(id -g)" \
        -v "$REPO_ROOT:/workspace" -w /workspace \
        "$IMAGE" \
        "./$REL_BIN" --gtest_filter="$FILTER"
}

run_with_unshare() {
    echo "==> Testing via unshare --net: $BINARY"
    unshare --net bash -c "
        set -euo pipefail
        ip link set lo up
        \"$BINARY\" --gtest_filter='$FILTER'
    "
}

if docker info &>/dev/null 2>&1; then
    run_in_docker
elif unshare --net true 2>/dev/null; then
    run_with_unshare
else
    echo "error: neither Docker nor unshare --net is available." >&2
    echo "  Option 1 (preferred): install/start Docker" >&2
    echo "  Option 2: sudo sysctl -w kernel.unprivileged_userns_clone=1" >&2
    exit 1
fi

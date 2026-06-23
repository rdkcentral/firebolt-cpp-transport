#!/usr/bin/env bash
# Build and run unit tests inside the CI Docker image. Like `cargo test`.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="firebolt-cpp-transport-ci:local"

# Build the CI image if not present
if ! docker image inspect "$IMAGE" &>/dev/null; then
    echo "Building CI Docker image (one-time)..."
    docker build -t "$IMAGE" -f "$SCRIPT_DIR/.github/Dockerfile" "$SCRIPT_DIR"
fi

RUN="docker run --rm -v $SCRIPT_DIR:/workspace $IMAGE"

# Configure if no cache or if the cache was made from a different source path
mkdir -p "$SCRIPT_DIR/build-dev"
cached_src=$($RUN bash -c "grep '^CMAKE_HOME_DIRECTORY' build-dev/CMakeCache.txt 2>/dev/null | cut -d= -f2" || true)
if [[ "$cached_src" != "/workspace" ]]; then
    echo "Configuring..."
    $RUN cmake -B build-dev -S . -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
fi

echo "Building..."
$RUN cmake --build build-dev --parallel

echo "Testing..."
$RUN ctest --test-dir build-dev/test --output-on-failure

if [[ "${ENABLE_COVERAGE:-0}" == "1" ]]; then
    echo "Coverage..."
    $RUN gcovr -r /workspace -f 'src/' -f 'include/' /workspace/build-dev \
        --print-summary --exclude-unreachable-branches --exclude-throw-branches
fi

#!/usr/bin/env bash
# Check (default) or fix clang-format. Like `cargo fmt [--check]`.
#   ./fmt.sh        — check only (exit 1 if violations)
#   ./fmt.sh --fix  — reformat in place
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="firebolt-cpp-transport-ci:local"

if ! docker image inspect "$IMAGE" &>/dev/null; then
    echo "Building CI Docker image (one-time)..."
    docker build -t "$IMAGE" -f "$SCRIPT_DIR/.github/Dockerfile" "$SCRIPT_DIR"
fi

RUN="docker run --rm -v $SCRIPT_DIR:/workspace $IMAGE bash -c"

if [[ "${1:-}" == "--fix" ]]; then
    $RUN "git config --global --add safe.directory /workspace && git ls-files -- '*.cpp' '*.h' | xargs clang-format -i"
    echo "Done. Files reformatted."
else
    $RUN "git config --global --add safe.directory /workspace && git ls-files -- '*.cpp' '*.h' | xargs clang-format --dry-run --Werror"
    echo "Formatting OK."
fi

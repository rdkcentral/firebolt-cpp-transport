#!/usr/bin/env bash
# cov_build.sh — configure and build firebolt-cpp-transport for Coverity.
# Installs dependencies then builds the library (tests excluded).
#
# Usage: sh cov_build.sh
set -x
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
sh "${SCRIPT_DIR}/build_dependencies.sh"

GITHUB_WORKSPACE="${GITHUB_WORKSPACE:-${PWD}}"
cd "${GITHUB_WORKSPACE}"

cmake -B build-dev -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=OFF

cmake --build build-dev --parallel

#!/usr/bin/env bash
# cov_build.sh — configure and build firebolt-cpp-transport
#
# Run from the repo root after build_dependencies.sh has prepared the
# environment.  Produces a Debug build with tests enabled so that
# Coverity can intercept the full compilation including test code.
#
# Usage: sh cov_build.sh
set -x
set -e

GITHUB_WORKSPACE="${GITHUB_WORKSPACE:-${PWD}}"
cd "${GITHUB_WORKSPACE}"

cmake -B build-dev -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=ON

cmake --build build-dev --parallel

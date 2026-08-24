#!/usr/bin/env bash
# cov_build.sh — configure and build firebolt-cpp-transport
#
# Run from the repo root. Tests are excluded — Coverity analyzes the library
# sources; test builds require GTest which the Coverity container lacks.
#
# Usage: sh cov_build.sh
set -x
set -e

GITHUB_WORKSPACE="${GITHUB_WORKSPACE:-${PWD}}"
cd "${GITHUB_WORKSPACE}"

cmake -B build-dev -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_TESTS=OFF

cmake --build build-dev --parallel

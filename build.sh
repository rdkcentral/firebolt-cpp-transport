#!/usr/bin/env bash

# Copyright 2025 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="firebolt-cpp-transport-ci:local"

# Pre-scan for --docker before arg parsing so forwarded args are preserved
use_docker=false
_forward_args=()
for _arg in "$@"; do
  [[ "$_arg" == "--docker" ]] && { use_docker=true; continue; }
  _forward_args+=("$_arg")
done

if $use_docker; then
  for _bdir in build build-dev; do
    _cache="$SCRIPT_DIR/$_bdir/CMakeCache.txt"
    if [[ -f "$_cache" ]]; then
      _cached=$(grep '^CMAKE_HOME_DIRECTORY' "$_cache" 2>/dev/null | cut -d= -f2 || true)
      if [[ -n "$_cached" && "$_cached" != "/workspace" ]]; then
        echo "Wiping stale $_bdir (configured at $_cached)..."
        rm -rf "$SCRIPT_DIR/$_bdir"
      fi
    fi
  done
  if ! docker image inspect "$IMAGE" &>/dev/null; then
    echo "Building CI Docker image (one-time)..."
    docker build -t "$IMAGE" -f "$SCRIPT_DIR/.github/Dockerfile" "$SCRIPT_DIR"
  fi
  exec docker run --rm --user "$(id -u):$(id -g)" \
    -v "$SCRIPT_DIR:/workspace" -w /workspace \
    "$IMAGE" ./build.sh "${_forward_args[@]}"
fi

bdir="build"
do_install=false
params=
buildType="Debug"
cleanFirst=false

while [[ ! -z $1 ]]; do
  case $1 in
  --clean) cleanFirst=true;;
  --release) buildType="Release";;
  --sysroot) SYSROOT_PATH="$2"; shift;;
  --legacy) params+=" -DENABLE_LEGACY_RPC_V1=ON";;
  -i | --install) do_install=true;;
  +tests) params+=" -DENABLE_TESTS=ON"; bdir="build-dev";;
  +gen-cov)
    set -e
    cd build-dev
    ctest --test-dir ./test
    mkdir -p coverage
    gcovr -r .. \
      --exclude '.*/test/.*\.h' \
      --exclude '.*/test/.*\.cpp' \
      --decisions \
      --medium-threshold 50 --high-threshold 75 \
      --html-details coverage/index.html \
      --cobertura coverage.cobertura.xml
    exit 0;;
  --) shift; break;;
  *) break;;
  esac; shift
done

SYSROOT_PATH="${SYSROOT_PATH:-/}"
[[ -e $SYSROOT_PATH ]] || { echo "SYSROOT_PATH does not exist ($SYSROOT_PATH)" >/dev/stderr; exit 1; }

$cleanFirst && rm -rf $bdir

if [[ ! -e "$bdir" || -n "$@" ]]; then
  params+=" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
  command -v ccache >/dev/null 2>&1 && params+=" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
  cmake -B $bdir \
    -DCMAKE_BUILD_TYPE=$buildType \
    -DSYSROOT_PATH=$SYSROOT_PATH \
    $params \
    "$@" || exit $?
fi
cmake --build $bdir --parallel || exit $?
if $do_install && [[ $bdir == 'build' ]]; then
  cmake --install $bdir || exit $?
fi

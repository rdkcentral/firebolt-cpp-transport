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

[[ ! -z $SYSROOT_PATH ]] || { echo "SYSROOT_PATH not set" >/dev/stderr; exit 1; }
[[ -e $SYSROOT_PATH ]] || { echo "SYSROOT_PATH not exist ($SYSROOT_PATH)" >/dev/stderr; exit 1; }

$cleanFirst && rm -rf $bdir

if [[ ! -e $bdir ]]; then
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

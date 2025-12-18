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

package="package"
version="0.1.0-unknown"
sub_path=
dst=

while [[ ! -z $1 ]]; do
  case $1 in
  --package) package="$2"; shift;;
  --version) version="$2"; shift;;
  --subpath) sub_path="$2"; shift;;
  --dst) dst="$2"; shift;;
  esac; shift
done

set -u

version="${version#v}"
dist_name="$package-$version"
dist_path="build/$dist_name"

mkdir -p "$dist_path"
rm -f build/$package-*.tar.gz

if [[ -z $sub_path ]]; then
  git checkout-index -af --prefix="$dist_path/"
else
  git ls-files $sub_path | git checkout-index --stdin -qf --prefix="$dist_path.tmp/"
  mv -T "$dist_path.tmp/$sub_path/" "$dist_path/"
fi
rm -rf $dist_path/package.json $dist_path/package-lock.json $dist_path/.releaserc.json $dist_path/.github
echo "$version" >$dist_path/.version
tar -czf "build/$dist_name.tar.gz" -C "build" "$dist_name"

if [[ ! -z "$dst" ]]; then
  mkdir -p "$dst"
  cp "build/$dist_name.tar.gz" "$dst/"
fi


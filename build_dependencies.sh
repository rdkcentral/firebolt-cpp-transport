#!/usr/bin/env bash
# build_dependencies.sh — install all build dependencies for firebolt-cpp-transport
#
# Installs to the system prefix (/usr/local) and is intentionally idempotent:
# running it multiple times is safe.  Mirrors the dep set and versions pinned in
# .github/Dockerfile so that both the native CI image and the Coverity container
# end up with an identical build environment.
#
# Usage: sh build_dependencies.sh
#        (run as root, or with sudo, from any directory)
set -x
set -e

DEPS_GOOGLETEST_V="1.15.2"
DEPS_NLOHMANN_JSON_V="3.11.3"
DEPS_JSON_SCHEMA_VALIDATOR_V="2.3.0"
DEPS_WEBSOCKETPP_V="0.8.2"

# ---------------------------------------------------------------------------
# 1. System packages
# ---------------------------------------------------------------------------
apt-get update
apt-get install -y --no-install-recommends --fix-missing \
    build-essential ca-certificates \
    cmake pkg-config clang-format \
    libboost-all-dev \
    curl wget git \
    python3-pip

if python3 -m pip help install | grep -q -- '--break-system-packages'; then
    python3 -m pip install --break-system-packages gcovr
else
    python3 -m pip install gcovr
fi

# ---------------------------------------------------------------------------
# 2. googletest
# ---------------------------------------------------------------------------
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

dir="googletest-${DEPS_GOOGLETEST_V}"
curl -sL "https://github.com/google/googletest/releases/download/v${DEPS_GOOGLETEST_V}/${dir}.tar.gz" \
    | tar xzf - -C "$WORK_DIR"
cmake -B "$WORK_DIR/build/${dir}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      "$WORK_DIR/${dir}"
cmake --build "$WORK_DIR/build/${dir}" --target install

# ---------------------------------------------------------------------------
# 3. nlohmann/json
# ---------------------------------------------------------------------------
dir="nlohmann-json-${DEPS_NLOHMANN_JSON_V}"
git clone --depth 1 --branch "v${DEPS_NLOHMANN_JSON_V}" \
    "https://github.com/nlohmann/json" "$WORK_DIR/${dir}"
cmake -B "$WORK_DIR/build/${dir}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DJSON_BuildTests=OFF \
      "$WORK_DIR/${dir}"
cmake --build "$WORK_DIR/build/${dir}" --target install

# ---------------------------------------------------------------------------
# 4. json-schema-validator
# ---------------------------------------------------------------------------
dir="json-schema-validator-${DEPS_JSON_SCHEMA_VALIDATOR_V}"
curl -sL "https://github.com/pboettch/json-schema-validator/archive/refs/tags/${DEPS_JSON_SCHEMA_VALIDATOR_V}.tar.gz" \
    | tar xzf - -C "$WORK_DIR"
cmake -B "$WORK_DIR/build/${dir}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DJSON_VALIDATOR_BUILD_TESTS=OFF \
      -DJSON_VALIDATOR_BUILD_EXAMPLES=OFF \
      "$WORK_DIR/${dir}"
cmake --build "$WORK_DIR/build/${dir}" --target install

# ---------------------------------------------------------------------------
# 5. websocketpp (header-only, cmake install registers package config)
# ---------------------------------------------------------------------------
dir="websocketpp-${DEPS_WEBSOCKETPP_V}"
curl -sL "https://github.com/zaphoyd/websocketpp/archive/refs/tags/${DEPS_WEBSOCKETPP_V}.tar.gz" \
    | tar xzf - -C "$WORK_DIR"
cmake -B "$WORK_DIR/build/${dir}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DBUILD_TESTS=OFF \
      -DBUILD_EXAMPLES=OFF \
      "$WORK_DIR/${dir}"
cmake --build "$WORK_DIR/build/${dir}" --target install

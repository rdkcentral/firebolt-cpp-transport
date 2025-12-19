FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends --fix-missing \
    build-essential ca-certificates \
    cmake pkg-config clang-format \
    libboost-all-dev \
    curl wget git \
    && apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /deps

ARG DEPS_GOOGLETEST_V="1.15.2"
RUN cd /deps \
    && dir="googletest-${DEPS_GOOGLETEST_V}" \
    && curl -sL https://github.com/google/googletest/releases/download/v${DEPS_GOOGLETEST_V}/$dir.tar.gz | tar xzf - \
    && cmake -B "build/$dir" \
          -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=ON \
          "$dir" \
    && cmake --build "build/$dir" --target install \
    && rm -rf "build/$dir" "$dir"

ARG DEPS_NLOHMANN_JSON_V="3.11.3"
RUN cd /deps \
    && dir="nlohmann-json-${DEPS_NLOHMANN_JSON_V}" \
    && git clone --depth 1 --branch "v${DEPS_NLOHMANN_JSON_V}" "https://github.com/nlohmann/json" "$dir" \
    && cmake -B "build/$dir" \
          -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=ON \
          -DJSON_BuildTests=OFF \
          "$dir" \
    && cmake --build "build/$dir" --target install \
    && rm -rf "build/$dir" "$dir"

ARG DEPS_JSON_SCHEMA_VALIDATOR_V="2.3.0"
RUN cd /deps \
    && dir="json-schema-validator-${DEPS_JSON_SCHEMA_VALIDATOR_V}" \
    && curl -sL https://github.com/pboettch/json-schema-validator/archive/refs/tags/${DEPS_JSON_SCHEMA_VALIDATOR_V}.tar.gz | tar xzf - \
    && cmake -B "build/$dir" \
          -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=ON \
          -DJSON_VALIDATOR_BUILD_TESTS=OFF \
          -DJSON_VALIDATOR_BUILD_EXAMPLES=OFF \
          "$dir" \
    && cmake --build "build/$dir" --target install \
    && rm -rf "build/$dir" "$dir"

ARG DEPS_WEBSOCKETPP_V="0.8.2"
RUN cd /deps \
    && dir="websocketpp-${DEPS_WEBSOCKETPP_V}" \
    && curl -sL https://github.com/zaphoyd/websocketpp/archive/refs/tags/${DEPS_WEBSOCKETPP_V}.tar.gz | tar xzf - \
    && cmake -B "build/$dir" \
          -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_SHARED_LIBS=ON \
          -DBUILD_TESTS=OFF \
          -DBUILD_EXAMPLES=OFF \
          "$dir" \
    && cmake --build "build/$dir" --target install \
    && rm -rf "build/$dir" "$dir"

RUN apt-get update && apt-get install -y --no-install-recommends --fix-missing \
    python3-pip \
    && apt-get autoremove -y && apt-get clean -y && rm -rf /var/lib/apt/lists/*

RUN pip install --break-system-packages gcovr

WORKDIR /workspace

CMD ["/bin/bash"]

# Copilot Instructions for firebolt-cpp-transport

## What This Is
A C++17 WebSocket + JSON-RPC 2.0 transport layer used by all Firebolt C++ SDKs.
It is a pure outbound proxy client — no listening sockets in production code.
The only server-side sockets are in unit test files (local mock WebSocket server).

## Source Layout
```
include/firebolt/   — public headers (gateway.h, helpers.h, json_types.h, types.h, logger.h)
src/                — implementation (gateway.cpp, transport.cpp, helpers_impl.cpp, utils.cpp, logger.cpp)
test/unit/          — GTest unit tests (*Test.cpp)
test/UnitTestsMain.cpp
.github/Dockerfile  — CI image (Ubuntu 24.04 + GTest, nlohmann_json, websocketpp, gcovr)
```

## Key Concepts
- **`IGateway`** — JSON-RPC 2.0 framing over websocketpp; all SDK calls go through here
- **`IHelper`** — typed `get<T>()` / `set()` / `invoke()` / `subscribe()` over IGateway
- **`Result<T>`** — `std::optional`-like return: check with `if (result)`, dereference with `*result`, error with `result.error()`
- **`Firebolt::Error`** enum — all error paths return this
- Protocol modes: `rpc_v2` (default) and `legacy` (v1, enabled with `-DENABLE_LEGACY_RPC_V1=ON`)

## Dev Scripts (all Docker-based — deps live in the CI image)
- `./test.sh` — build + run unit tests (equivalent of `cargo test`)
- `./fmt.sh` — check formatting (equivalent of `cargo fmt --check`)
- `./fmt.sh --fix` — reformat in place
- `./build.sh +tests` — manual CMake build with tests enabled (requires `SYSROOT_PATH`)

## Build System
- CMake, build output in `build-dev/` when tests enabled
- The CI Docker image (`firebolt-cpp-transport-ci:local`) has all deps pre-installed
- Do NOT run cmake directly on the host — deps are only in the Docker image
- `test.sh` / `fmt.sh` auto-build the image on first run from `.github/Dockerfile`

## Code Style
- C++17, clang-format enforced (run `./fmt.sh --fix` before committing)
- Match existing patterns — no `auto` for non-obvious types, explicit return types on methods
- New unit tests go in `test/unit/` as `*Test.cpp`, using GTest + GMock
- No new dependencies without updating `.github/Dockerfile`

## CI
- Format check, build, unit tests, and coverage all run in the CI Docker image
- Coverage generated with gcovr; reports uploaded as artifacts
- Branch targets: `main`, maintenance branches (`X.Y.x-maintenance`), RC branches (`X.Y-rc`)
- Tag format for releases: `vX.Y.Z` (see CHANGELOG.md)

## Common Pitfalls
- `build-dev/` created inside Docker is owned by root — if you see permission errors, `sudo rm -rf build-dev/` and re-run `./test.sh`
- `build-dev/CMakeCache.txt` bakes in the source path as `/workspace`; if it was built elsewhere the stale check in `test.sh` will wipe and reconfigure automatically

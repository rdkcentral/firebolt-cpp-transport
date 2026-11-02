# Firebolt C++ Transport

A C++17 WebSocket + JSON-RPC 2.0 transport library shared by all Firebolt C++ SDKs. It handles connection management, request/response correlation, and event subscriptions — so higher-level SDKs only deal with typed method calls.

## Architecture

```
Your App / Firebolt SDK
    │
    ▼
IHelper  (get<T> / set / invoke / subscribe)
    │
    ▼
IGateway  (JSON-RPC 2.0 framing over WebSocket)
    │
    ▼
ws://127.0.0.1:3474/jsonrpc  (Firebolt gateway daemon — out of process)
```

All production code is outbound-only. No listening sockets are opened by this library.

## Public API

### `Result<T>` — return type for all calls

```cpp
auto result = helper.get<MyJsonType, std::string>("module.method");
if (result) {
    use(*result);          // has a value
} else {
    auto e = result.error(); // Firebolt::Error enum
    auto info = result.errorInfo(); // Firebolt::ErrorInfo (code + message)
    // info.error() gives the error code (int32_t)
    // info.message() gives the error message (std::string)
}
```

### `IGateway` — low-level JSON-RPC transport
```cpp
IGateway& gw = Firebolt::Transport::GetGatewayInstance();
gw.connect(config, onConnectionChange);
auto future = gw.request("module.method", params);
gw.subscribe("module.onEvent", callback, userdata);
gw.unsubscribe("module.onEvent", userdata);
gw.disconnect();
```

Local subscribers using the same exact event name share one upstream
subscription, including mixed-case module names such as
`Lifecycle2.onStateChanged`. Removing one local subscriber retains upstream
delivery until the last subscriber is removed. Use the same wire spelling for
subscribe, unsubscribe, and notifications; event names are not case-folded.

### `IHelper` — typed wrapper over IGateway
```cpp
IHelper& helper = Firebolt::Helpers::GetHelperInstance();
auto result = helper.get<DeviceIdJson, std::string>("device.id");
helper.invoke("lifecycle.ready", {});
SubscriptionId id = helper.subscribe<EventJson>("device.onNameChanged", callback, userdata);
helper.unsubscribe("device.onNameChanged", id);
```

### `Firebolt::Error` — error codes
| Value | Meaning |
|---|---|
| `None` | Success |
| `NotConnected` | No active WebSocket connection |
| `Timedout` | Request timed out |
| `CapabilityNotPermitted` | App lacks the required capability |
| `InvalidRequest` / `InvalidParams` | Bad JSON-RPC request |

## Building

### Test-only ACK wait observation

`-DFIREBOLT_TRANSPORT_ACK_OBSERVER=ON` enables an internal Linux test observer;
it is **OFF by default and must not be enabled for release builds**. No public
SDK API is added. A test controller may pass an inherited nonblocking pipe
descriptor in `FIREBOLT_ACK_OBSERVER_FD`. Without that variable, the observer
build emits nothing. Invalid descriptors or incomplete writes fail explicitly.

Each subscribe/unsubscribe wait emits newline-delimited JSON entry/exit records
with the wire operation ID, listen flag, original active-time budget,
CLOCK_MONOTONIC start/observation timestamps and ready-future outcome. The
controller owns SIGSTOP/SIGCONT and must independently prove that all threads
stopped before the original budget expired, with no exit record at stop. Entry
is observation, not a blocking barrier; no timeout or poll slice is extended.
Unsubscribe's ordinary success return on ACK timeout is unchanged: an exit
record with `ready: false` cannot qualify ACK receipt.

Observer SDK artifacts require separate provenance. Their local test evidence
does not qualify production release builds, actual Dobby/memcr or GUI survival.

Dependencies (GTest, nlohmann_json, websocketpp, gcovr) are only available inside the CI Docker image. Use the helper scripts:

```bash
./test.sh          # build + run unit tests  (like cargo test)
./fmt.sh           # check formatting        (like cargo fmt --check)
./fmt.sh --fix     # reformat in place
```

The image is built automatically from `.github/Dockerfile` on first run.

For cross-compilation against a sysroot (e.g. for device targets):
```bash
SYSROOT_PATH=/path/to/sysroot ./build.sh --release
```

## Hibernation / Process Freeze

When the hosting container is hibernated the process is frozen (e.g. `SIGSTOP`, cgroup freezer) and later resumed. The transport detects this from the gap in its watchdog clock and adapts:

| `Config` field | Default | Meaning |
|---|---|---|
| `waitTime_ms` | 3000 | Request timeout, counted in **active** time only; stopped time is excluded |
| `stopGapThreshold_ms` | 2000 | Wall-clock gap treated as a stop. Clamped to at least 4 x `watchdogCycle_ms` |
| `resumeGrace_ms` | 5000 | After a detected stop, pending requests and subscribe acknowledgements are not failed before this much active time has passed. `0` disables |

```cpp
gw.setResumeListener([](std::chrono::milliseconds stoppedFor) {
    // Runs on the notification worker thread; may issue requests (e.g. re-query state).
});
gw.setResumeListener(nullptr); // clear
```

The transport is lifecycle-agnostic: it only reports that a stop happened. Reconciling application state (e.g. lifecycle) is the client library's job.

Tests: `test/unit/hibernationTest.cpp` and `activeClockTest.cpp` use an injected clock (deterministic); `test/integration/hibernationHarness.cpp` freezes a real forked child with `SIGSTOP`/`SIGCONT`. Both run as part of `./test.sh`. To run only the harness after a build:

```bash
docker run --rm -v $PWD:/workspace firebolt-cpp-transport-ci:local \
    bash -c "ctest --test-dir build-dev/test -L hibernation --output-on-failure"
```

## Project Layout
```
include/firebolt/   public headers (gateway.h, helpers.h, json_types.h, types.h, logger.h)
src/                implementation
test/unit/          GTest unit tests (*Test.cpp)
test/integration/   integration tests and the SIGSTOP hibernation harness
.github/Dockerfile  CI image definition
```

## License
Apache 2.0 — see [LICENSE](LICENSE)

## Header Support

The transport layer supports custom HTTP headers during WebSocket connection setup and allows retrieval of response headers from the server.

### Sending Custom Headers

You can specify custom headers in the `Firebolt::Config` struct:

```cpp
Firebolt::Config config;
config.wsUrl = "ws://localhost:9002";
config.headers["Authorization"] = "Bearer <token>";
config.headers["X-Custom-Header"] = "value";
```

Pass the config to the gateway's `connect` method:

```cpp
Firebolt::Transport::IGateway& gateway = Firebolt::Transport::GetGatewayInstance();
gateway.connect(config, onConnectionChange);
```

### Retrieving Response Headers

After a successful connection, you can retrieve response headers sent by the server:

```cpp
std::optional<std::string> value = gateway.getResponseHeader("Server");
if (value) {
    std::cout << "Server header: " << *value << std::endl;
}
```

- If the header is present, its value is returned.
- If the header is not present, `std::nullopt` is returned.
- Header-name lookup is case-insensitive, as required by HTTP; header values are returned unchanged.

#### Thread Safety
Header operations are thread-safe. Access to response headers is protected by a mutex internally.

#### API Reference
- [Config struct](include/firebolt/config.h.in)
- [IGateway interface](include/firebolt/gateway.h)
- [websocketpp connection API - get_response_header](https://docs.websocketpp.org/classwebsocketpp_1_1connection.html#a72e0c94609844078fc611716c39791de)

## Runtime Logging Overrides (Environment Variables)

To make logging configurable without changing app code, the transport resolves `FIREBOLT_TRANSPORT_LOG_LEVEL` during `IGateway::connect()` (changes require reconnect) and reads `FIREBOLT_TRANSPORT_LOG_FILE` on each log call.
When set, `FIREBOLT_TRANSPORT_LOG_LEVEL` overrides `config.log.level` passed by the app. Log lines are prefixed with `[FireboltNative|<module>|<level>]`.

- `FIREBOLT_TRANSPORT_LOG_LEVEL`: `off|error|warning|notice|info|debug` (or `0..4`) — resolved once at connect time; overrides `config.log.level` passed by the app. Changes require reconnect.
- `FIREBOLT_TRANSPORT_LOG_FILE`: absolute path to append logs to (relative paths are ignored for safety); if unset, logs go to stderr (or syslog when built with `ENABLE_SYSLOG`). Read on each log call, so changes take effect immediately.
- Note: changing environment variables at runtime is not guaranteed to be thread-safe on all platforms; prefer setting these variables before starting the transport, or avoid updating them while multiple threads are logging.

Example:

```bash
export FIREBOLT_TRANSPORT_LOG_LEVEL=debug
export FIREBOLT_TRANSPORT_LOG_FILE=/opt/logs/firebolt-transport.log
```

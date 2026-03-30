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

### `IHelper` — typed wrapper over IGateway
```cpp
IHelper& helper = Firebolt::Helpers::GetHelperInstance();
auto result = helper.get<DeviceIdJson, std::string>("device.id");
helper.set("device.name", params);
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

## Project Layout
```
include/firebolt/   public headers (gateway.h, helpers.h, json_types.h, types.h, logger.h)
src/                implementation
test/unit/          GTest unit tests (*Test.cpp)
.github/Dockerfile  CI image definition
```

## License
Apache 2.0 — see [LICENSE](LICENSE)


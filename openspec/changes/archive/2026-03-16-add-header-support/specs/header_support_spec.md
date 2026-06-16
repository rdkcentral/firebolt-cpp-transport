# Header Support Specification

## Overview
Add support for custom HTTP headers in the Firebolt C++ transport layer, including both header injection during connection and response header retrieval after connection.

## Description
This specification defines the expected API and behavior for sending custom HTTP headers during the WebSocket handshake and retrieving response headers after a successful connection. The `Config` struct is extended with a `headers` field, and a new `getResponseHeader()` method is exposed on `IGateway` to allow callers to inspect server response headers after connect.

## Requirements
- Clients MUST be able to specify custom headers in the `Config` struct.
- The transport layer MUST inject these headers into the WebSocket handshake via `replace_header` before connecting.
- The transport layer MUST provide a method to retrieve response headers by name after connection.
- Header operations MUST be thread-safe.
- If a header is not present, the retrieval method MUST return `std::nullopt`.

## Architecture / Design
- Custom headers are stored in `Config.headers` (`std::map<std::string, std::string>`).
- On `Transport::connect()`, all entries in `Config.headers` are applied to the websocketpp connection object via `con->replace_header(key, value)` before `client_->connect(con)` is called.
- On connection open (`Transport::onOpen`), server response headers are read from the websocketpp connection's HTTP response and cached in `responseHeaders_` (protected by `responseHeadersMutex_`).
- `Transport::getResponseHeader()` and `GatewayImpl::getResponseHeader()` provide thread-safe read access to the cached response headers.

## External Interfaces
- `Config.headers: std::map<std::string, std::string>` — map of header name to value; populated by the caller before `connect()`.
- `IGateway::getResponseHeader(const std::string& headerName): std::optional<std::string>` — returns the value of the named response header, or `std::nullopt` if absent.

**Example:**
```cpp
Firebolt::Config config;
config.headers["Authorization"] = "Bearer <token>";
Firebolt::Transport::IGateway& gateway = Firebolt::Transport::GetGatewayInstance();
gateway.connect(config, onConnectionChange);

std::optional<std::string> serverHeader = gateway.getResponseHeader("Server");
if (serverHeader) {
    std::cout << "Server: " << *serverHeader << std::endl;
}
```

**Notes:**
- Header names are case-sensitive.
- Not all servers will echo custom headers in the response.
- Standard headers such as `Sec-WebSocket-Accept` are available after a successful connection.

## Performance
_Not applicable — header processing occurs only during connection setup and is negligible in cost relative to the WebSocket handshake itself._

## Security
- Headers are a common vector for injecting authentication tokens (e.g., `Authorization: Bearer <token>`). Callers are responsible for keeping token values secret.
- Do not log header values at default log levels; sensitive data (tokens, credentials) must not appear in logs.
- The response header cache is only populated on a successful open; there is no risk of stale data from a failed connection.

## Versioning & Compatibility
- Backward compatible: the `headers` field on `Config` defaults to an empty map; existing callers require no changes.
- `getResponseHeader()` is a new virtual method on `IGateway`; all existing mock/stub implementations must add it (non-breaking for the library API, but mock classes need updating).

## Conformance Testing & Validation
- Unit test: `TransportIntegrationUTest.HeaderInjectionAndResponseHeaderRetrieval` in `test/unit/transportTest.cpp` verifies that a connection can be established with custom headers and that standard response headers (e.g., `Sec-WebSocket-Accept`) are retrievable after connect.
- `MockGateway` in `test/unit/helperTest.cpp` exposes `getResponseHeader` via `MOCK_METHOD` to satisfy the interface contract.

## Covered Code
- `src/transport.cpp`:
    - `Transport::connect`
    - `Transport::onOpen`
    - `Transport::getResponseHeader`
- `src/transport.h`:
    - `Transport::getResponseHeader`
- `src/gateway.cpp`:
    - `GatewayImpl::getResponseHeader`
- `include/firebolt/config.h.in`:
    - `Firebolt::Config::headers`
- `include/firebolt/gateway.h`:
    - `IGateway::getResponseHeader`

---

## Open Queries
_No open queries._

## References
- [Design: Add Header Support](../design.md)
- [websocketpp connection API - get_response_header](https://docs.websocketpp.org/classwebsocketpp_1_1connection.html#a72e0c94609844078fc611716c39791de)
- [websocketpp connection API - replace_header](https://docs.websocketpp.org/classwebsocketpp_1_1connection.html)

## Change History
- 2026-06-16 - Archived/updated in this PR.
- 2026-03-16 - satlead - Initial spec for header support (inject request headers, retrieve response headers)

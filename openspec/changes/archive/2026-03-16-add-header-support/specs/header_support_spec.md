# Header Support Specification

## Overview
Add support for custom HTTP headers in the Firebolt C++ transport layer, including both header injection during connection and response header retrieval after connection.

## Description
This specification defines the expected API and behavior for sending custom HTTP headers during the WebSocket handshake and retrieving response headers after a successful connection.
## Requirements
- Clients MUST be able to specify custom headers in the `Config` struct.
- The transport layer MUST inject these headers into the WebSocket handshake.
- The transport layer MUST provide a method to retrieve response headers by name after connection.
- Header operations MUST be thread-safe.
- If a header is not present, the retrieval method MUST return `std::nullopt`.

## API
- `Config.headers: std::map<std::string, std::string>`
- `IGateway::getResponseHeader(const std::string&): std::optional<std::string>`

## Behavior
- On connect, all headers in `Config.headers` are sent with the WebSocket handshake.
- After connection, response headers are available via `getResponseHeader`.
- If a header is not present, `getResponseHeader` returns `std::nullopt`.
- All access to response headers is thread-safe.

## Example
```cpp
Firebolt::Config config;
config.headers["Authorization"] = "Bearer <token>";
Firebolt::Transport::IGateway& gateway = Firebolt::Transport::GetGatewayInstance();
gateway.connect(config, onConnectionChange);

std::optional<std::string> serverHeader = gateway.getResponseHeader("Server");
```

## Notes
- Header names are case-sensitive.
- Not all servers will echo custom headers in the response.

## References
- [Design: Add Header Support](../design.md)
- [websocketpp connection API - get_response_header](https://docs.websocketpp.org/classwebsocketpp_1_1connection.html#a72e0c94609844078fc611716c39791de)

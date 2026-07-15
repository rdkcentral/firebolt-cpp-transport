# Design: Add Header Support to Transport Configuration and Connect Method

## Overview
This document details the design for adding custom header support to the Firebolt C++ transport layer. The goal is to allow clients to specify custom headers during WebSocket connection setup and to retrieve response headers from the server.

## Goals
- Allow clients to inject custom HTTP headers during WebSocket connection setup.
- Allow clients to retrieve response headers after connection is established.
- Ensure thread safety and robust error handling for header operations.

## Non-Goals
- TLS/SSL enhancements (handled separately)
- Protocol-level changes outside header handling

## Design Details
### Config Struct
- Add a `headers` field to `Firebolt::Config`:
  - Type: `std::map<std::string, std::string>`
  - Default: empty
- Example:
  ```cpp
  Firebolt::Config config;
  config.headers["Authorization"] = "Bearer <token>";
  ```

### Transport Layer
- Update the `connect` method to accept and forward headers to the WebSocket connection.
- Inject headers using `replace_header` on the websocketpp connection object before connecting.
- On successful connection, retrieve response headers using `get_response_header` and store them in a thread-safe map.
- Provide a method `getResponseHeader(const std::string&)` to retrieve a header value by name.
- Protect all access to the response headers map with a mutex for thread safety.

### IGateway Interface
- Expose `getResponseHeader(const std::string&)` to clients.

### Error Handling
- If the connection pointer is null or header retrieval fails, log an error and return `std::nullopt`.
- If a header is not present, return `std::nullopt`.

## Thread Safety
- All access to the response headers map is protected by a mutex.
- Header injection occurs before connection is established, so no race conditions are expected.

## API Reference
- [Config struct](../../../../include/firebolt/config.h.in)
- [IGateway interface](../../../../include/firebolt/gateway.h)
- [websocketpp connection API - get_response_header](https://docs.websocketpp.org/classwebsocketpp_1_1connection.html#a72e0c94609844078fc611716c39791de)

## Alternatives Considered
- Using a vector of pairs for headers (less efficient for lookup)
- Not exposing response headers (limits integration options)

## Risks
- Some WebSocket servers may not echo custom headers in the response.
- Header names are case-insensitive per HTTP spec, but are treated as case-sensitive in the map.

## Migration
- Backward compatible: existing clients can ignore the new headers field.

## Open Questions
- Should header name lookup be case-insensitive?

---

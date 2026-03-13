# Firebolt Transport Layer Change Proposal

## Title
Add Header Support to Transport Configuration and Connect Method

## Overview
This proposal introduces support for custom headers in the Firebolt transport layer. It allows clients to specify headers during connection setup and retrieve response headers from the server, improving flexibility for authentication and integration.

## Problem Statement
Currently, the transport layer does not support custom headers for WebSocket connections, limiting authentication and integration options. Clients also lack access to response headers returned by the server.

## Proposed Solution
- Extend the `Config` object to include a `headers` field (map of string to string).
- Update the `connect` method to accept and use these headers during connection setup.
- Provide a method for clients to retrieve a specific response header by name:
  - `std::optional<std::string> getResponseHeader(const std::string& headerName);`
  - Returns the header value if present, otherwise null/empty.

## Design Details
- `Config` struct: Add `std::map<std::string, std::string> headers;` (default: empty).
- `connect` method: Accept and forward headers to the underlying WebSocket implementation.
- New method: `getResponseHeader(const std::string& headerName)` returns the value for the requested header.
- Ensure thread safety and proper error handling for header operations.

## Compatibility & Migration
- Backward compatible: Existing clients can ignore the new headers field.
- No impact on legacy protocol support.

## Performance & Security
- Minimal performance impact; header processing is lightweight.
- Enables flexible authentication and security schemes via custom headers.

## Testing & Validation
- Add tests for header injection and retrieval.
- Validate header propagation and response handling.

## Alternatives Considered
- Embedding headers in URL query parameters (rejected for security and flexibility reasons).

## Risks & Mitigations
- Risk: Misuse of headers for unsupported authentication schemes.
- Mitigation: Document supported header usage and error handling.

## Tasks & Timeline
- Update `Config` struct and connect method.
- Implement response header retrieval.
- Add unit and integration tests.
- Update documentation.

## References
- transport_layer_spec.md
- config.h.in

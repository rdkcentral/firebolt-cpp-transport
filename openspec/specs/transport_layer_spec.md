# Firebolt Transport Layer Spec

## Overview
The Firebolt Transport Layer provides an event-driven interface for connecting, disconnecting, and managing subscriptions to transport events. It is designed for extensibility and integration with Firebolt-enabled systems.

---

## Description
The transport layer is a standalone C++ library that abstracts WebSocket communication and JSON-RPC message handling. It exposes `IGateway` as the primary public interface for consumers. Internally, a `Transport` class manages the WebSocket connection lifecycle, message dispatching, and callback registration. The library is intentionally decoupled from client business logic and API schemas, enabling reuse across different Firebolt SDK implementations.

---

## Requirements
- Expose `IGateway` as the sole public interface for consumers.
- Support connect, disconnect, send, request, subscribe, and unsubscribe operations.
- Support configurable WebSocket URL, timeouts, watchdog, and logging.
- Surface connection state changes via `ConnectionChangeCallback`.
- Handle JSON-RPC requests and responses with unique message IDs.
- Support legacy RPC v1 event notification format via `legacyRPCv1` config flag.
- Provide custom response header retrieval after connection.

---

## Architecture / Design

### Architectural Sketch

```
┌───────────────┐
│ IGateway      │
│  ├─ connect   │
│  ├─ disconnect│
│  └─ Callbacks │
└─────▲─────────┘
      │
┌─────┴─────────┐
│ Transport     │
│ (WebSocket)   │
└─────▲─────────┘
      │
┌─────┴─────────┐
│ Helpers       │
│ (Subscriptions│
└───────────────┘
```

- `IGateway` is the public interface — consumers interact only with this.
- `Transport` (internal, `src/transport.h`) manages the WebSocket connection and message dispatching.
- `Helpers` / `IHelper` wrap `IGateway` for typed property access, invocation, and subscription management.

### Connection Protocol

- **WebSocket Transport**: The client connects to a configurable URL (default: `ws://127.0.0.1:9998`). Connection state is managed via `ConnectionChangeCallback`.
- **JSON-RPC Messaging**: All requests, responses, and events are encoded as JSON-RPC messages. The transport layer handles serialization/deserialization using nlohmann::json.
- **Legacy Support**: The `legacyRPCv1` flag enables compatibility with older event notification formats.

```
┌─────────────┐      WebSocket      ┌─────────────┐
│   Client    │<------------------->│   Gateway   │
└─────────────┘     JSON-RPC        └─────────────┘
```

### Extensibility
- Callback-based integration for event and connection handling.
- Template-based notification for property changes.
- Flexible payloads via native types and JSON serialization.

---

## External Interfaces

### Configuration Mapping

The `Firebolt::Config` structure defines all options available during connection:

| Field                | Type                          | Default             | Description                              |
|----------------------|-------------------------------|---------------------|------------------------------------------|
| wsUrl                | std::string                   | ws://127.0.0.1:9998 | WebSocket endpoint                       |
| headers              | std::map<std::string, std::string> | {}                  | Custom headers for WebSocket handshake   |
| waitTime_ms          | unsigned                      | 3000                | RPC response timeout (ms)                |
| legacyRPCv1          | bool                          | macro value         | Enable legacy event notification         |
| log.level            | LogLevel                      | Info                | Log verbosity                            |
| log.transportInclude | optional<unsigned>            | n/a                 | Log category include mask                |
| log.transportExclude | optional<unsigned>            | n/a                 | Log category exclude mask                |
| log.format.ts        | bool                          | true                | Timestamp in logs                        |
| log.format.location  | bool                          | false               | Source location in logs                  |
| log.format.function  | bool                          | true                | Function name in logs                    |
| log.format.thread    | bool                          | true                | Thread id in logs                        |
| watchdogCycle_ms     | unsigned                      | 500                 | Watchdog polling interval (ms)           |

### Event Types & Error Handling

- Transport events are surfaced via `EventCallback`:
  - Connection state changes (connected, disconnected, failed)
  - Incoming JSON-RPC notifications (custom event types)
  - Property change notifications (via template callbacks)
- Errors are surfaced via `ConnectionChangeCallback` and response callbacks.
- JSON-RPC error responses include an error object with code and message mapped to `Firebolt::Error`.
- Timeout and watchdog settings in `Config` provide additional error detection.
- Legacy error formats are supported when `legacyRPCv1` is enabled.

### Transport Interface (Internal)

The `Transport` class is internal (`src/transport.h`) and not exposed via public headers. It provides:
- `connect(url, onMessage, onConnectionChange, ...)`: Establishes WebSocket connection, registers callbacks.
- `disconnect()`: Terminates the connection.
- `getNextMessageID()`: Generates unique message IDs.
- `send(method, params, id)`: Sends JSON-RPC messages.
- `getResponseHeader(name)`: Retrieves a response header after connection.

---

## Performance
_Not applicable — no performance requirements are defined for this spec. See [transport_recommendations_spec.md](transport_recommendations_spec.md) for performance recommendations._

---

## Security
_Not applicable — security is out of scope for this spec. See [transport_recommendations_spec.md](transport_recommendations_spec.md) for security recommendations._

---

## Versioning & Compatibility
- The `legacyRPCv1` flag enables compatibility with legacy event notification formats.
- The transport layer is versioned as part of the `FireboltTransport` library (see `cmake/version.cmake`).
- See [transport_recommendations_spec.md](transport_recommendations_spec.md) for versioning and compatibility recommendations.

---

## Conformance Testing & Validation
_Not applicable — conformance test strategies are defined in [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Covered Code

- include/firebolt/gateway.h:
    - IGateway::connect
    - IGateway::disconnect
    - IGateway::send
    - IGateway::request
    - IGateway::subscribe
    - IGateway::unsubscribe
    - IGateway::getResponseHeader
    - GetGatewayInstance
- include/firebolt/config.h.in:
    - Config
- src/transport.h:
    - Transport::connect
    - Transport::disconnect
    - Transport::send
    - Transport::getNextMessageID
    - Transport::getResponseHeader
- src/gateway.cpp:
    - Gateway::connect
    - Gateway::disconnect
    - Gateway::send
    - Gateway::request
    - Gateway::subscribe
    - Gateway::unsubscribe
    - Gateway::getResponseHeader

---

## Open Queries
- Integration points with external systems (e.g., Firebolt SDK) are not yet mapped — how should `IGateway` initialization be documented for external consumers?
- Externalized header usage and examples for initializing and configuring `Gateway` are missing — should these be added to this spec or a separate integration guide?
- `Subscription Lifecycle & Integration Points` was identified as incomplete — what additional integration points need to be documented?

---

## References
- [header_interfaces_spec.md](header_interfaces_spec.md)
- [cpp_specifics_spec.md](cpp_specifics_spec.md)
- [transport_recommendations_spec.md](transport_recommendations_spec.md)
- [json_rpc_handling_spec.md](json_rpc_handling_spec.md)

---

## Change History
- 2026-06-16 - Added/updated in this PR.
- 2026-04-17 - Restructured to match spec template; moved header interface details to header_interfaces_spec.md.

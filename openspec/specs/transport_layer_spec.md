# Firebolt Transport Layer Spec

## Overview
The Firebolt Transport Layer provides an event-driven interface for connecting, disconnecting, and managing subscriptions to transport events. It is designed for extensibility and integration with Firebolt-enabled systems.

---

## Key Interfaces


---

## Architectural Sketch

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

---

## Extensibility
- Callback-based integration for event and connection handling.
- Template-based notification for property changes.
- Flexible payloads via native types and JSON serialization.

---

## Subscription Lifecycle & Integration Points


---

## Unknowns & Gaps
- Integration points with external systems (e.g., Firebolt SDK) are not yet mapped.

## Event Types & Error Handling

### Event Types
- Transport events are surfaced via `EventCallback` and include:
  - Connection state changes (connected, disconnected, failed)
  - Incoming JSON-RPC notifications (custom event types)
  - Property change notifications (via template callbacks)
- Event types are identified by method names in JSON-RPC messages.
- Consumers can register callbacks for specific event types.

### Error Handling
- Errors are surfaced via `ConnectionChangeCallback` and response callbacks.
- JSON-RPC error responses include an error object with code and message.
- Transport errors (e.g., connection failures) are mapped to `Firebolt::Error` and passed to callbacks.
- Timeout and watchdog settings in `Config` provide additional error detection and recovery.
- Legacy error formats are supported when `legacyRPCv1` is enabled.

**Conclusion:**
Event types are extensible via JSON-RPC method names, and error handling is integrated into callback flows for both transport and protocol-level failures.

## Configuration Mapping

The `Firebolt::Config` structure defines the set of options available during connection:

| Field                | Type         | Default                | Description                                  |
|----------------------|--------------|------------------------|----------------------------------------------|
| wsUrl                | std::string  | ws://127.0.0.1:9998    | WebSocket endpoint                           |
| waitTime_ms          | unsigned     | 3000                   | RPC response timeout (ms)                    |
| legacyRPCv1          | bool         | macro value            | Enable legacy event notification             |
| log.level            | LogLevel     | Info                   | Log verbosity                                |
| log.transportInclude | optional<unsigned> | n/a              | Log category include mask                    |
| log.transportExclude | optional<unsigned> | n/a              | Log category exclude mask                    |
| log.format.ts        | bool         | true                   | Timestamp in logs                            |
| log.format.location  | bool         | false                  | Source location in logs                      |
| log.format.function  | bool         | true                   | Function name in logs                        |
| log.format.thread    | bool         | true                   | Thread id in logs                            |
| watchdogCycle_ms     | unsigned     | 500                    | Watchdog polling interval (ms)               |

Log settings are highly configurable, supporting both level and category masks. Legacy RPC support is toggled via macro, suggesting build-time flexibility. Watchdog and timeout values are tunable for performance and reliability.

## Connection Protocol

The Firebolt Transport Layer uses WebSocket as its underlying transport, providing a persistent, bidirectional channel for communication. JSON-RPC is layered on top of WebSocket, enabling structured remote procedure calls and event notifications.

- **WebSocket Transport**
  - The client connects to a configurable WebSocket URL (default: ws://127.0.0.1:9998).
  - Connection state is managed via callbacks (e.g., ConnectionChangeCallback).
  - Transport supports reconnect, disconnect, and error handling.

- **JSON-RPC Messaging**
  - All requests, responses, and events are encoded as JSON-RPC messages.
  - Each message includes method, params, id, and (for responses) result or error.
  - Event notifications are sent as JSON-RPC messages with specific method names.
  - The transport layer handles serialization/deserialization using nlohmann::json.

- **Legacy Support**
  - The `legacyRPCv1` flag enables compatibility with older event notification formats.

- **Message Flow**

  ┌─────────────┐      WebSocket      ┌─────────────┐
  │   Client    │<------------------->│   Gateway   │
  └─────────────┘     JSON-RPC        └─────────────┘

  - Client sends JSON-RPC requests over WebSocket.
  - Gateway processes requests, sends responses/events.
  - Both sides can initiate messages (bidirectional).

## Transport Interface Clarification

- The `Transport` class is defined internally (src/transport.h) and is not exposed via public headers.
- Public consumers interact with the transport layer via the `IGateway` interface, which wraps or delegates to a `Transport` instance.
- The `Transport` class provides:
  - `connect(url, onMessage, onConnectionChange, transportLoggingInclude, transportLoggingExclude)`: Establishes WebSocket connection, registers callbacks, configures logging.
  - `disconnect()`: Terminates the connection.
  - `getNextMessageID()`: Generates unique message IDs.
  - `send(method, params, id)`: Sends JSON-RPC messages.
- Integration relies on callback registration for message and connection events.
- Logging is configurable via include/exclude masks.

**Conclusion:**
The transport layer is designed for internal use, with IGateway serving as the public API. Consumers interact with IGateway, which manages transport details and exposes connection, event, and message handling.

## Integration Points

- The Firebolt Transport Layer is a standalone library focused on converting incoming calls to JSON-RPC for WebSocket transport.
- It is agnostic to client usage and the underlying WebSocket implementation, enabling flexible integration with various consumers.
- Actual API schemas and business logic reside in separate repositories (e.g., https://github.com/rdkcentral/firebolt-cpp-client).
- IGateway and IHelper interfaces are designed for external use, but there are currently no examples or documentation for initializing and configuring Gateway.
- Building clear initialization and configuration flows for Gateway is an open requirement for the spec.

**Conclusion:**
The transport library is decoupled from client and schema logic, providing a generic, extensible foundation for JSON-RPC over WebSocket. Integration patterns and initialization flows should be defined to support external consumers and future SDKs.

## Externalized Headers & Usage Findings


## Refactoring Note

Header interface details (IHelper, SubscriptionManager, json_types, types.h, logger.h) have been moved to a dedicated spec: `header_interfaces_spec.md`. The transport layer spec now focuses solely on transport mechanics, protocol, and architecture.

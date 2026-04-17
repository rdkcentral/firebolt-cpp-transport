# Firebolt JSON-RPC Handling & Callback Spec

## Overview
This document describes how JSON-RPC messages are handled within the Firebolt Transport Layer, including request/response processing, event notification, and callback integration.

---

## Description
All communication in the Firebolt transport layer occurs over WebSocket using the JSON-RPC 2.0 protocol. The transport layer serializes outgoing requests and deserializes incoming responses and notifications using `nlohmann::json`. Callbacks are registered at runtime for event subscriptions, connection state changes, and property updates. The transport layer tracks outstanding requests by ID and dispatches responses to the appropriate callbacks when received.

---

## Requirements
- All messages must conform to JSON-RPC 2.0 format (request, response, notification).
- Requests must include a unique `id` for response correlation.
- Incoming responses must be matched to outstanding requests by `id` and dispatched to the appropriate callback.
- Event notifications (no `id`) must be dispatched to registered event callbacks by method name.
- Unregistered event notifications must be logged and ignored (no crash or silent failure).
- Callbacks must be dynamically registerable and unregisterable at runtime.
- Event names are case-sensitive.
- Timeout errors must be surfaced to the caller via `Result<T>` if a response is not received within `waitTime_ms`.

---

## Architecture / Design

### Processing Flow

```
┌─────────────┐   WebSocket   ┌─────────────┐
│   Client    │<------------->│   Gateway   │
└─────────────┘   JSON-RPC    └─────────────┘
      │                          │
      │  Request/Notification    │
      └─────────────────────────▶│
      │                          │
      │      Response/Event      │
      ◀──────────────────────────┘
```

### Request/Response Callback Flow

```
Client sends JSON-RPC request (with id)
    │
    ▼
Transport layer stores callback for id
    │
    ▼
Gateway sends response (with same id)
    │
    ▼
Transport layer matches id, triggers callback
    │
    ▼
Callback receives parsed JSON result/error
```

### Event Notification Callback Flow

```
Gateway sends JSON-RPC notification (event)
    │
    ▼
Transport layer parses event, finds callback
    │
    ▼
Callback receives parsed JSON event payload
```

---

## External Interfaces

### Message Types

| Type         | Format                                                                              |
|--------------|-------------------------------------------------------------------------------------|
| Request      | `{ "jsonrpc": "2.0", "method": "<name>", "params": { ... }, "id": <number> }`      |
| Response     | `{ "jsonrpc": "2.0", "result": { ... }, "id": <number> }`                          |
| Error        | `{ "jsonrpc": "2.0", "error": { ... }, "id": <number> }`                           |
| Notification | `{ "jsonrpc": "2.0", "method": "<event>", "params": { ... } }`                     |

### Callback Types

- **EventCallback**: `std::function<void(void* usercb, const nlohmann::json& params)>` — invoked for incoming event notifications.
- **ConnectionChangeCallback**: `std::function<void(const bool connected, const Firebolt::Error error)>` — invoked on connection/disconnection and error states.
- **onPropertyChangedCallback**: Template-based — used for property change notifications, supports flexible payloads.

### Callback Registration

- `IGateway::subscribe(event, callback, usercb)` — registers an event callback.
- `IGateway::unsubscribe(event, usercb)` — unregisters an event callback.
- `IHelper::subscribe(owner, eventName, notification, callback)` — higher-level subscription with typed callbacks.

---

## Performance
_Not applicable — no specific performance requirements for JSON-RPC message handling. See [transport_recommendations_spec.md](transport_recommendations_spec.md) for performance recommendations._

---

## Security
_Not applicable — security is out of scope for this spec. See [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Versioning & Compatibility
- The `legacyRPCv1` flag enables compatibility with older event notification formats.
- JSON-RPC 2.0 is the target protocol; legacy support is maintained via config.

---

## Conformance Testing & Validation
- Error and callback handling must follow JSON-RPC 2.0 message formats.
- Notification callbacks should adhere to JSON-RPC design (no `id` field).
- See [transport_recommendations_spec.md](transport_recommendations_spec.md) for conformance test strategies.

---

## Covered Code

- include/firebolt/gateway.h:
    - IGateway::request
    - IGateway::subscribe
    - IGateway::unsubscribe
    - IGateway::send
- src/gateway.cpp:
    - Gateway::request
    - Gateway::subscribe
    - Gateway::unsubscribe
    - Gateway::send
- src/transport.h:
    - Transport::send
    - Transport::onMessage
    - Transport::startMessageWorker
    - Transport::processQueuedMessages
- include/firebolt/helpers.h:
    - IHelper::subscribe
    - IHelper::unsubscribe
    - onPropertyChangedCallback

---

## Open Queries
- Should retry logic for failed or timed-out requests be added to the transport layer, or remain client-driven?
- Should batch JSON-RPC request/response handling be supported? What are the use cases?
- Should a catch-all or default handler be supported for unregistered event types?
- Are all error codes in `Firebolt::Error` fully surfaced to callers, including error messages from JSON-RPC error objects?

---

## References
- [transport_layer_spec.md](transport_layer_spec.md)
- [header_interfaces_spec.md](header_interfaces_spec.md)
- [transport_recommendations_spec.md](transport_recommendations_spec.md)

---

## Change History
- 2026-04-17 - Restructured to match spec template.

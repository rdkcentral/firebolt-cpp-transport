# Firebolt JSON-RPC Handling & Callback Spec

## Overview
This document describes how JSON-RPC messages are handled within the Firebolt Transport Layer, including request/response processing, event notification, and callback integration.

---

## JSON-RPC Message Handling

- **Transport**: All communication occurs over WebSocket using JSON-RPC format.
- **Message Types**:
  - **Request**: `{ "jsonrpc": "2.0", "method": "<name>", "params": { ... }, "id": <number> }`
  - **Response**: `{ "jsonrpc": "2.0", "result": { ... }, "id": <number> }` or `{ "jsonrpc": "2.0", "error": { ... }, "id": <number> }`
  - **Notification**: `{ "jsonrpc": "2.0", "method": "<event>", "params": { ... } }`
- **Serialization**: Uses nlohmann::json for encoding/decoding.

---

## JSON Serialization & Deserialization

- **Library Used**: The Firebolt Transport Layer uses a JSON serialization/deserialization library appropriate to the implementation language.
- **Features**:
  - Provides a modern interface for working with JSON objects.
  - Supports conversion between native types and JSON with minimal boilerplate.
  - Handles parsing, validation, and error reporting for JSON-RPC messages.
- **Usage**:
  - Incoming WebSocket messages are parsed into JSON objects.
  - Outgoing requests, responses, and notifications are constructed as JSON and serialized to string.
  - Callbacks receive parsed JSON payloads for flexible handling.
- **Advantages**:
  - Strong type safety and expressive syntax.
  - Widely used and actively maintained open-source library.
  - Integrates seamlessly with native containers and custom types.

---

## Callback Integration

- **EventCallback**: Invoked for incoming event notifications.
  - Provides user context and parsed JSON parameters.
- **ConnectionChangeCallback**: Invoked on connection/disconnection and error states.
- **Property Change Callback**: Template-based, e.g. `onPropertyChangedCallback<void*, nlohmann::json>`
  - Used for property change notifications, supports flexible payloads.

---

## Callback Registration

- Gateway exposes Subscribe and Unsubscribe methods for event callback management.
- Event names are agnostic to the transport layer but are case sensitive.
- EventCallback is used to register callbacks for specific event types.
- The transport layer implements JSON-RPC mechanisms for subscribe and unsubscribe, as defined in the Firebolt JSON-RPC Specification.
- Callbacks can be dynamically registered and unregistered at runtime, allowing flexible event handling.

---

## Callback Handling Details

### Request/Response Callback Flow

- When a JSON-RPC request is sent, it includes a unique id.
- The transport layer tracks outstanding requests by id.
- Upon receiving a JSON-RPC response (result or error) with a matching id, the corresponding callback is triggered.
- The callback receives the parsed JSON result or error payload, allowing application logic to process the response.

**Flow Diagram:**

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

---

### Event Notification Callback Flow

- JSON-RPC notifications (events) are sent without an `id`.
- The transport layer parses the event and determines the registered callback for the event type (method name).
- The callback is invoked with the parsed JSON event payload and user context.

**Flow Diagram:**

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

## Notification Handling

- If a JSON-RPC notification arrives and no callback is registered for its event type, the transport layer logs a warning and ignores the event.
- There is no catch-all or fallback callback mechanism; unhandled notifications are logged for visibility.
- Clients cannot currently register a default handler for unknown or unexpected event types.
- This behavior ensures that unhandled events are surfaced for debugging and monitoring, but are not processed further.

---

## Processing Flow

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

- Client sends JSON-RPC requests or notifications.
- Gateway processes, invokes relevant callbacks, and sends responses/events.
- Callbacks handle parsed JSON payloads and user context.

---

## Extensibility
- Callbacks are std::function, allowing lambdas, function pointers, or bound methods.
- Template-based property change callbacks support flexible event types.
- JSON-RPC method names and params are extensible for new features.

---

## Error Handling Analysis

### Current Mechanisms
- **Timeouts**: Requests are tracked with timestamps; if a response is not received within the configured wait time, a timeout error (`Firebolt::Error::Timedout`) is set and logged.
- **Send Failures**: If sending a request fails, the error is immediately propagated to the caller and the request is removed from the queue.
- **JSON-RPC Error Responses**: If a response contains an `"error"` field, the error code is mapped to a `Firebolt::Error` and set in the result.
- **Result Handling**: All errors are wrapped in a `Result<nlohmann::json>` object, allowing consumers to check for success or failure.
- **Exception Handling**: Out-of-range exceptions (e.g., missing receiver for a response) are caught and logged, but do not propagate further.
- **Logging**: Errors and timeouts are logged for visibility.

### Summary Table

| Error Type         | Handling Mechanism                  | Propagation         |
|--------------------|------------------------------------|---------------------|
| Timeout            | Set error, log, remove from queue   | Result object       |
| Send failure       | Set error, remove from queue        | Result object       |
| JSON-RPC error     | Map code, set error in result       | Result object       |
| Exception (lookup) | Catch, log info                     | No propagation      |

### Gaps & Unknowns
- No retry logic for failed or timed-out requests.
- No batch request handling or advanced JSON-RPC error features.
- Error codes are mapped but not all details (e.g., error messages) are surfaced.

---

## Addressing Remaining Gaps

### Retry Logic
- **Current State**: The transport layer does not automatically retry failed or timed-out requests. Errors are surfaced to the caller, who may implement custom retry logic if desired.
- **Recommendation**: To support robust communication, consider adding configurable retry policies for transient errors and timeouts. This could include exponential backoff, maximum retry count, and error-specific handling.

### Batch Request Handling
- **Current State**: The transport layer does not support JSON-RPC batch requests (multiple requests sent as an array). All requests are handled individually.
- **Recommendation**: To improve efficiency and support advanced JSON-RPC features, consider implementing batch request handling. This would require tracking multiple request IDs, aggregating responses, and updating callback flows to handle arrays of results/errors.

### Error Code Recommendations
- **Current State**: Error codes from JSON-RPC responses are mapped to internal `Firebolt::Error` values, but not all error details (such as error messages or additional data) are surfaced to the caller.
- **Recommendation**: Enhance error handling by exposing the full error object from JSON-RPC responses, including code, message, and any additional data. This will provide richer context for debugging and allow clients to make more informed decisions based on error specifics.

**Conclusion:**
These enhancements would strengthen the transport layer’s reliability and compliance with the JSON-RPC specification, supporting more advanced client use cases.

---

## Custom Extensions

- The transport layer is agnostic to public API definitions and private proprietary implementations.
- Mechanisms exist for clients to define and extend custom JSON-RPC methods or event types; the transport is oblivious to the data payloads.
- API information and schema are documented separately, ensuring transport remains decoupled and generic.
- Extensions and custom payloads can be freely added by clients, with no constraints imposed by the transport layer.

---

## Concurrency & Thread Safety

- JSON-RPC handling is asynchronous; clients are expected to implement their own thread safety and synchronization as needed.
- Message processing occurs on an arrival basis and is handled asynchronously.
- Further analysis is required to identify potential race conditions and deadlocks in the codebase.
- Documentation on concurrency and thread safety is currently lacking and should be substantiated in the spec for future clarity and best practices.

---

## Resource Cleanup

- The transport layer ensures graceful shutdown by stopping worker threads and clearing all event-to-callback mappings and notification queues.
- The Server destructor calls stopNotificationWorker and clears eventList, guaranteeing cleanup of local maps and callback associations.
- Request/response queues and maps are erased as responses are processed or on timeout, preventing memory leaks.
- This approach matches expectations for proper resource cleanup and safe shutdown of the transport layer.

---

## Versioning & Compatibility

- The transport layer supports both legacy and JSON-RPC compliant versions of Firebolt.
- Protocol version is mapped to the `RPCv2` URL parameter in the WebSocket connection; presence of this parameter indicates use of the compliant JSON-RPC protocol.
- The intention is to move toward full JSON-RPC compliance, but legacy support remains for existing apps.
- Version negotiation and compatibility are handled via connection parameters, not through explicit JSON-RPC version fields.

### Example: Using `usercb` for Context-Aware Callbacks

```cpp
// Registering a subscription with usercb
void* myContext = /* pointer to custom state */;
gateway.Subscribe("eventName", [](void* usercb, const nlohmann::json& params) {
    // Cast usercb to your custom type
    MyType* context = static_cast<MyType*>(usercb);
    // Use context and params to handle the event
    context->handleEvent(params);
}, myContext);

// Unsubscribing using the same usercb
gateway.Unsubscribe("eventName", myContext);
```

- When the event fires, the callback receives `usercb` and can access custom state or objects.
- Unsubscribe uses `usercb` to identify and remove the correct subscription.

This pattern enables context-aware callbacks and precise subscription management.


# Firebolt C++ Specifics Spec

## Overview
This spec consolidates all C++-specific implementation details, patterns, and library choices for the Firebolt transport library. It separates C++-centric information from general transport and protocol specs for easier cross-language adoption.

---

## Description
The C++ implementation of the Firebolt transport library uses `nlohmann::json` for JSON serialization/deserialization, `websocketpp` for WebSocket transport, and C++ STL types for data structures. Callback patterns use `std::function`, and subscription management uses RAII via `SubscriptionManager`. Template-based patterns are used for typed property change notifications. Interface signatures are C++-idiomatic and designed for use by client SDK libraries.

---

## Requirements
- Use `nlohmann::json` for all JSON serialization/deserialization.
- Use `websocketpp` for WebSocket transport (with support for custom header injection via `replace_header`).
- Use `std::function` for all callback types.
- Provide RAII-based subscription cleanup via `SubscriptionManager`.
- Use C++ templates for typed property change notifications (`onPropertyChangedCallback`).
- Use `std::any` for flexible payloads in subscription data.
- Use `types.h` for `Result<T>` and `Error` for consistent API contracts.

---

## Architecture / Design

### WebSocket Connection & Message Management

The transport layer uses `websocketpp` to establish and manage WebSocket connections:

- **Connection setup**:
  - Calls `client_->get_connection(url, ec)` to create a connection object.
  - Injects custom headers using `con->replace_header(key, value)` before connecting.
  - Registers `open`, `fail`, `close`, and `message` handlers via `websocketpp::lib::bind`.
  - Calls `client_->connect(con)` to initiate the connection.
- **Message management**:
  - Registers a message handler to process incoming messages and invoke callbacks.
  - Uses `onMessage`, `onOpen`, `onClose`, and `onFail` methods for connection lifecycle events.
  - Handles connection state, error logging, and callback invocation for client integration.

---

## External Interfaces

### Callback Signatures

- `EventCallback`: `std::function<void(void* usercb, const nlohmann::json& params)>`
- `ResponseCallback`: `std::function<void(Firebolt::Result<nlohmann::json>)>`
- `onPropertyChangedCallback<JsonType, Args...>(void* subscriptionDataPtr, const nlohmann::json& jsonResponse)`

### Interface Signatures (C++ Examples)

IGateway request:
```cpp
virtual std::future<Firebolt::Result<nlohmann::json>> request(
    const std::string& method,
    const nlohmann::json& parameters) = 0;
```

IHelper property access:
```cpp
Result<bool> result = helper_.get<Firebolt::JSON::Boolean, bool>("Discovery.watched", parameters);
```

Enum serialization:
```cpp
parameters["agePolicy"] = Firebolt::JSON::toString(Firebolt::JsonData::AgePolicyEnum, *agePolicy);
```

### RAII & Template Usage
- `SubscriptionManager` provides RAII cleanup and template-based notifications.
- Type-safe, template-based integration for client libraries.

### Error Handling & Result Types
- All APIs return `Result<T>` or `Result<void>` wrapping a value or `Firebolt::Error`.
- Consistent API contracts via C++ types defined in `types.h`.

---

## Performance
_Not applicable — no C++-specific performance requirements defined. See [transport_recommendations_spec.md](transport_recommendations_spec.md) for general performance recommendations._

---

## Security
_Not applicable — security is not C++-specific. See [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Versioning & Compatibility
_Not applicable — versioning is managed at the library level. See [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Conformance Testing & Validation
_Not applicable — conformance testing is addressed in [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Covered Code

- src/transport.h:
    - Transport::connect
    - Transport::onMessage
    - Transport::onOpen
    - Transport::onClose
    - Transport::onFail
    - Transport::setLogging
- include/firebolt/helpers.h:
    - onPropertyChangedCallback
    - IHelper::get
    - IHelper::subscribe
- include/firebolt/json_types.h:
    - NL_Json_Basic::fromJson
    - NL_Json_Basic::value
    - NL_Json_Basic::checkRequiredFields
    - toString
- src/helpers_impl.h:
    - HelperImpl::set
    - HelperImpl::invoke
    - HelperImpl::subscribe
    - HelperImpl::unsubscribeAll
    - SubscriptionManager::unsubscribeAll

---

## Open Queries
- Are there plans to support other JSON libraries (e.g., RapidJSON) or make the JSON library pluggable for cross-platform builds?
- Should `websocketpp` be replaceable with another WebSocket library (e.g., for platforms where it is unavailable)?
- Are RAII patterns for `SubscriptionManager` fully documented for external SDK authors?

---

## References
- [transport_layer_spec.md](transport_layer_spec.md)
- [header_interfaces_spec.md](header_interfaces_spec.md)
- [json_rpc_handling_spec.md](json_rpc_handling_spec.md)

---

## Change History
- 2026-04-17 - Restructured to match spec template.

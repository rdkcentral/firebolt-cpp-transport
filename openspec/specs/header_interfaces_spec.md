# Firebolt Header Interfaces Spec

## Overview
This spec documents the key header interfaces provided by the Firebolt transport library, including their abstractions, usage patterns, and extension points. These interfaces are intended for client libraries and SDKs to enable robust integration with the transport layer.

---

## Description
The Firebolt transport library exposes a set of public header interfaces (`IHelper`, `SubscriptionManager`, `json_types`, `types.h`, `logger.h`) designed for use by client libraries. These interfaces provide type-safe property access, method invocation, subscription lifecycle management, JSON serialization utilities, error types, and logging. They are designed to be stable, cross-language-aligned abstractions over the underlying transport mechanics.

---

## Requirements
- `IHelper` must be externalized for client library use.
- `SubscriptionManager` must be externalized for client library use.
- `json_types` must provide standardized serialization/deserialization utilities for enums and structured data.
- `types.h` must define `Result`, `Error`, `LogLevel`, and `SubscriptionId` for consistent API contracts.
- `logger.h` should remain internal unless logging customization is required by external consumers.

---

## Architecture / Design
_Not applicable — these are interface headers, not an architectural component. See [transport_layer_spec.md](transport_layer_spec.md) for architecture._

---

## External Interfaces

### IHelper
- Manages property access, method invocation, and subscription lifecycle.
- Enables type-safe integration for client libraries.
- Key methods:
  - `set(methodName, parameters)` — sets a property via JSON-RPC.
  - `invoke(methodName, parameters)` — invokes a method via JSON-RPC.
  - `get<JsonType, PropertyType>(methodName, parameters)` — gets a typed property.
  - `subscribe(owner, eventName, notification, callback)` — subscribes to an event.
  - `unsubscribe(id)` — unsubscribes by ID.
  - `unsubscribeAll(owner)` — unsubscribes all events for an owner.

### SubscriptionManager
- Simplifies subscription handling for clients.
- Provides resource cleanup (RAII) and notification.
- `unsubscribeAll()` — cleans up all active subscriptions.

### json_types
- Provides standardized serialization/deserialization utilities for enums and structured data.
- Key templates/functions:
  - `NL_Json_Basic<T>::fromJson(json)` — deserializes a JSON value to native type.
  - `NL_Json_Basic<T>::value()` — returns the native value.
  - `NL_Json_Basic<T>::checkRequiredFields(json, fields)` — validates required fields.
  - `toString(enumType, value)` — serializes an enum value to string.

### types.h
- Defines `Result<T>` for return type wrapping.
- Defines `Error` enum for error codes.
- Defines `LogLevel` enum.
- Defines `SubscriptionId` type alias.

### logger.h
- Not used by client libraries; can remain internal unless logging customization is required.
- `Logger::setLogLevel(level)` — sets the active log level.
- `Logger::setFormat(ts, location, function, threadId)` — configures log format.
- `Logger::log(level, module, file, function, line, format, ...)` — logs a message.
- `Logger::isLogLevelEnabled(level)` — checks if a level is active.

---

## Performance
_Not applicable — performance requirements are not defined for header interfaces._

---

## Security
_Not applicable — security is not in scope for header interfaces. See [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Versioning & Compatibility
_Not applicable — versioning is managed at the library level. See [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Conformance Testing & Validation
_Not applicable — conformance testing is addressed in [transport_recommendations_spec.md](transport_recommendations_spec.md)._

---

## Covered Code

- include/firebolt/helpers.h:
    - IHelper::set
    - IHelper::invoke
    - IHelper::get
    - IHelper::subscribe
    - IHelper::unsubscribe
    - IHelper::unsubscribeAll
    - onPropertyChangedCallback
- src/helpers_impl.h:
    - HelperImpl::set
    - HelperImpl::invoke
    - HelperImpl::subscribe
    - HelperImpl::unsubscribe
    - HelperImpl::unsubscribeAll
    - SubscriptionManager::unsubscribeAll
- include/firebolt/types.h:
    - Result
    - Error
    - LogLevel
    - SubscriptionId
- include/firebolt/json_types.h:
    - NL_Json_Basic::fromJson
    - NL_Json_Basic::value
    - toString
- include/firebolt/logger.h:
    - Logger::setLogLevel
    - Logger::setFormat
    - Logger::log
    - Logger::isLogLevelEnabled

---

## Open Queries
- Should `logger.h` be externalized to allow consumer-controlled logging customization? Under what conditions?
- Are there plans to version `IHelper` independently for ABI stability across SDK releases?

---

## References
- [transport_layer_spec.md](transport_layer_spec.md)
- [cpp_specifics_spec.md](cpp_specifics_spec.md)

---

## Change History
- 2026-04-17 - Restructured to match spec template.

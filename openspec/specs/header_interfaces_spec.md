# Firebolt Header Interfaces Spec

## Overview
This spec documents the key header interfaces provided by the Firebolt transport library, including their abstractions, usage patterns, and extension points. These interfaces are intended for client libraries and SDKs to enable robust integration with the transport layer.

---

## IHelper
- Manages property access, method invocation, and subscription lifecycle.
- Enables type-safe integration for client libraries.

---

## SubscriptionManager
- Simplifies subscription handling for clients.
- Provides resource cleanup and notification.

---

## json_types
- Provides standardized serialization/deserialization utilities for enums and structured data.
- Ensures type safety and consistency across language implementations.

---

## types.h
- Used for Result and error handling.
- Important for consistent API contracts across clients.

---

## logger.h
- Not used by client libraries; can remain internal unless logging customization is required by external consumers.

---

## Recommendations
- Externalize IHelper, SubscriptionManager, json_types, and types.h for client library use.
- Keep logger.h internal unless needed for external logging integration.

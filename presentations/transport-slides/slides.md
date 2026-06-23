---
theme: default
title: Firebolt C++ Transport
info: |
  Slide deck for the Firebolt C++ Transport project
highlighter: shiki
transition: slide-left
mdc: true
---

# Firebolt C++ Transport

Event-driven, extensible transport layer for Firebolt-enabled systems.

---

# Transport Layer Overview

- Event-driven interface for connecting, disconnecting, and managing subscriptions
- Extensible and integrates with Firebolt SDK
- Callback-based event and connection handling
- Supports native types and JSON serialization

---

# Architectural Sketch

```
┌───────────────┐
│ IGateway      │
│  ├─ connect   │
│  ├─ disconnect│
│  └─ Callbacks │
└──────▲────────┘
  │
┌─────┴───────┐
│ Transport    │
│ (WebSocket)  │
└─────▲───────┘
  │
┌─────┴───────┐
│ Helpers      │
│ (Subscriptions│
└──────────────┘
```

---

# Connection Protocol

The Firebolt Transport Layer uses WebSocket as its underlying transport, with JSON-RPC layered on top for structured messaging and event notifications.

**Message Flow:**

```
┌─────────────┐      WebSocket      ┌─────────────┐
│   Client    │<------------------->│   Gateway   │
└─────────────┘     JSON-RPC        └─────────────┘
```

- Client connects to a configurable WebSocket URL (default: ws://127.0.0.1:9998)
- Connection state managed via callbacks (e.g., ConnectionChangeCallback)
- Supports reconnect, disconnect, and error handling
- All requests, responses, and events are JSON-RPC messages
- Legacy support via `legacyRPCv1` flag

---

# Integration Points

- Standalone library for JSON-RPC over WebSocket
- Agnostic to client usage and WebSocket implementation
- API schemas and business logic reside in separate repos (e.g., firebolt-cpp-client)
- IGateway and IHelper interfaces are designed for external use
- Initialization/configuration flows for Gateway are an open requirement

**Conclusion:**
The transport library is decoupled from client and schema logic, providing a generic, extensible foundation for JSON-RPC over WebSocket. Integration patterns and initialization flows should be defined to support external consumers and SDKs.

---

# Key Recommendations

- Add robust support for JSON-RPC batch requests
- Make retry logic optional and client-driven
- Ensure full JSON-RPC compliance (batch, error reporting)
- Provide async batch handling and granular error reporting
- Document thread safety, callback registration, and error handling
- Move header interfaces to dedicated specs for modularity

---

# Batch Requests: Pros & Cons

**Pros:**
- Reduces network overhead
- Improves throughput
- Enables atomic operations for grouped requests
- Simplifies client logic for bulk actions

**Cons:**
- Increases error handling complexity
- May block on slowest operation
- Harder to debug individual failures
- Risk of partial success

**Trade-offs:**
- Prefer async batch handling
- Provide granular error reporting
- Allow client choice between batch and single requests

---

# Event Types & Error Handling

- Connection state changes (connected, disconnected, failed)
- Incoming JSON-RPC notifications (custom event types)
- Property change notifications (template callbacks)
- Register callbacks for specific event types

---

# Documentation & Testing

- Improve docs for initialization, configuration, concurrency, cleanup
- Develop conformance tests and cross-language validation

---
layout: center
---

# Thank You!

[GitHub](https://github.com/rdkcentral/firebolt-cpp-transport)

---

# Connection Protocol

The Firebolt Transport Layer uses WebSocket as its underlying transport, with JSON-RPC layered on top for structured messaging and event notifications.

**Message Flow:**

```
┌─────────────┐      WebSocket      ┌─────────────┐
│   Client    │<------------------->│   Gateway   │
└─────────────┘     JSON-RPC        └─────────────┘
```

- Client connects to a configurable WebSocket URL (default: ws://127.0.0.1:9998)
- Connection state managed via callbacks (e.g., ConnectionChangeCallback)
- Supports reconnect, disconnect, and error handling
- All requests, responses, and events are JSON-RPC messages
- Legacy support via `legacyRPCv1` flag

---

# Integration Points

- Standalone library for JSON-RPC over WebSocket
- Agnostic to client usage and WebSocket implementation
- API schemas and business logic reside in separate repos (e.g., firebolt-cpp-client)
- IGateway and IHelper interfaces are designed for external use
- Initialization/configuration flows for Gateway are an open requirement

**Conclusion:**
The transport library is decoupled from client and schema logic, providing a generic, extensible foundation for JSON-RPC over WebSocket. Integration patterns and initialization flows should be defined to support external consumers and SDKs.
---

# Firebolt C++ Transport

A modern, efficient transport layer for Firebolt.

---

# Project Overview

- C++ implementation
- Modular design
- JSON-RPC support
- Logging and diagnostics

---

# Architecture

- Gateway
- Helpers
- Transport core
- Utilities

---

# Key Features

- High performance
- Extensible
- Easy integration
- Comprehensive unit tests

---
layout: center
---

# Thank You!

[GitHub](https://github.com/rdkcentral/firebolt-cpp-transport)

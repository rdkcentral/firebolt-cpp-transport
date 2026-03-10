# Firebolt Transport Recommendations Spec

## Overview
This document captures key recommendations for improving the Firebolt transport layer and its interfaces, based on exploration and analysis.

---

## Transport Layer
- Add robust support for JSON-RPC batch requests (send, request, subscribe, unsubscribe).
- Make retry logic optional and client-driven; prioritize quick error bubbling unless retry is likely to succeed.
- Ensure full JSON-RPC compliance, including batch request handling and error reporting.

### Batch Pros, Cons & Trade-offs
**Pros:**
  - Reduces network overhead
  - Improves throughput
  - Enables atomic operations for grouped requests
  - Simplifies client logic for bulk actions
**Cons:**
  - Increases complexity in error handling
  - May block on slowest operation in batch
  - Harder to debug individual failures
  - Risk of partial success
**Trade-offs:**
  - Prefer async batch handling to avoid blocking
  - Provide granular error reporting
  - Allow clients to choose between batch and single requests based on use case

---

## IGateway Interface
- Consider replacing or supplementing future-based request with a callback-based variant for non-blocking, async response handling.
- Document thread safety, callback registration, and error handling expectations.
- Add batch request methods with clear, extensible signatures.

---

## Externalized Interfaces
- Move header interfaces (IHelper, SubscriptionManager, json_types, types.h) to a dedicated spec for modularity.
- Externalize IHelper and SubscriptionManager for client library use.
- Standardize serialization/deserialization utilities for enums and structured data.

---

## Examples & Usage Patterns
- Provide examples for context-aware callbacks, property access, and enum serialization.
- Encourage idiomatic, language-specific patterns for each client library.

---

## Documentation & Testing
- Improve documentation for initialization, configuration, concurrency, and resource cleanup.
- Develop conformance tests and cross-language validation suites.

---

## Versioning & Compatibility
- Support both legacy and JSON-RPC compliant protocols; negotiate via WebSocket URL parameters (e.g., RPCv2).
- Move toward full JSON-RPC compliance for new apps.

---

## Security & Performance
- Consider input validation, authentication, and performance tuning (batching, thread management).

---

## Future Enhancements
- Plan for multi-language transport libraries (Dart, JS, TypeScript) using unified specs.
- Modularize specs for easier cross-repo adoption and evolution.

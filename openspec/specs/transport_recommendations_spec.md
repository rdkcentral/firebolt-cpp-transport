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

## Security
- Security and authentication must be handled at the server level.
- The transport layer should expose all available options for authentication and security during the connection phase (e.g., token, credentials, TLS).
- Recommendation: Allow custom headers to be specified during connection setup to support authentication schemes and additional security requirements.

---

## Performance
- The transport layer should have minimal knowledge of payload and authentication.
- Performance criteria: maximize throughput by minimizing processing and validation.
- Only requirement: ensure JSON validity, which is typically provided by the interface object in the specific language implementation.

---

## Future Enhancements
- Plan for multi-language transport libraries (C, Dart, JS, TypeScript) using unified specs.
- Modularize specs for easier cross-repo adoption and evolution.
- For each new language repo, reuse transport, JSON-RPC, header interfaces, and recommendations specs, and create a language-specific implementation spec (e.g., cpp_specifics_spec.md, dart_specifics_spec.md, js_specifics_spec.md).
- Expectation: Each language repo will follow the same interface abstractions and protocol compliance, adapting idiomatic patterns and libraries as needed for the target language.

---

## Conformance Testing & Validation
- Focus on JSON validity and protocol compliance across implementations.
- Error and callback handling must follow JSON-RPC message formats as defined in the protocol and header interfaces.
- Notification callbacks should also adhere to JSON-RPC design.
- Batch request support is optional; implement only if justified by use case and trade-off analysis.

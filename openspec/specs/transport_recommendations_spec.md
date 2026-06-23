# Firebolt Transport Recommendations Spec

## Overview
This document captures key recommendations for improving the Firebolt transport layer and its interfaces, based on exploration and analysis.

---

## Description
This spec is a recommendations document, not a normative specification. It captures findings, trade-off analyses, and guidance for future improvements to the Firebolt transport layer. It covers recommendations for the transport layer, IGateway interface, externalized interfaces, documentation, testing, versioning, security, performance, and future multi-language plans.

---

## Requirements
- Transport layer recommendations are non-binding unless explicitly adopted into a normative spec.
- All recommendations should be reviewed and triaged before implementation.
- Future language-specific repos should reuse transport, JSON-RPC, header interfaces, and recommendations specs, and create a language-specific implementation spec.

---

## Architecture / Design

### Transport Layer
- Add robust support for JSON-RPC batch requests (send, request, subscribe, unsubscribe).
- Make retry logic optional and client-driven; prioritize quick error bubbling unless retry is likely to succeed.
- Ensure full JSON-RPC compliance, including batch request handling and error reporting.

#### Batch Pros, Cons & Trade-offs
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

### IGateway Interface
- Consider replacing or supplementing future-based request with a callback-based variant for non-blocking, async response handling.
- Document thread safety, callback registration, and error handling expectations.
- Add batch request methods with clear, extensible signatures.

### Future Enhancements
- Plan for multi-language transport libraries (C, Dart, JS, TypeScript) using unified specs.
- Modularize specs for easier cross-repo adoption and evolution.
- For each new language repo, reuse transport, JSON-RPC, header interfaces, and recommendations specs, and create a language-specific implementation spec (e.g., `cpp_specifics_spec.md`, `dart_specifics_spec.md`, `js_specifics_spec.md`).
- Each language repo will follow the same interface abstractions and protocol compliance, adapting idiomatic patterns and libraries as needed.

---

## External Interfaces

### Externalized Interfaces
- Move header interfaces (`IHelper`, `SubscriptionManager`, `json_types`, `types.h`) to a dedicated spec for modularity.
- Externalize `IHelper` and `SubscriptionManager` for client library use.
- Standardize serialization/deserialization utilities for enums and structured data.

### Examples & Usage Patterns
- Provide examples for context-aware callbacks, property access, and enum serialization.
- Encourage idiomatic, language-specific patterns for each client library.

---

## Performance
- The transport layer should have minimal knowledge of payload and authentication.
- Performance criteria: maximize throughput by minimizing processing and validation.
- Only requirement: ensure JSON validity, which is typically provided by the interface object in the specific language implementation.

---

## Security
- Security and authentication must be handled at the server level.
- The transport layer should expose all available options for authentication and security during the connection phase (e.g., token, credentials, TLS).
- Recommendation: Allow custom headers to be specified during connection setup to support authentication schemes and additional security requirements.

---

## Versioning & Compatibility
- Support both legacy and JSON-RPC compliant protocols; negotiate via WebSocket URL parameters (e.g., RPCv2).
- Move toward full JSON-RPC compliance for new apps.

---

## Conformance Testing & Validation
- Focus on JSON validity and protocol compliance across implementations.
- Error and callback handling must follow JSON-RPC message formats as defined in the protocol and header interfaces.
- Notification callbacks should also adhere to JSON-RPC design.
- Batch request support is optional; implement only if justified by use case and trade-off analysis.
- Improve documentation for initialization, configuration, concurrency, and resource cleanup.
- Develop conformance tests and cross-language validation suites.

---

## Covered Code

- include/firebolt/gateway.h:
    - IGateway::connect
    - IGateway::disconnect
    - IGateway::request
    - IGateway::subscribe
    - IGateway::unsubscribe
    - GetGatewayInstance
- src/transport.h:
    - Transport::connect
    - Transport::disconnect
    - Transport::send
    - Transport::getResponseHeader
- src/utils.h:
    - buildGatewayUrl
- include/firebolt/helpers.h:
    - IHelper::set
    - IHelper::invoke
    - IHelper::get
    - IHelper::subscribe
    - IHelper::unsubscribe
    - IHelper::unsubscribeAll

---

## Open Queries
- Should batch JSON-RPC request handling be implemented? What is the priority and target timeline?
- Should the `IGateway::request` method be supplemented with a callback-based variant, or replaced entirely?
- What is the plan for multi-language SDK adoption — which language is next after C++?
- Are conformance tests planned as part of the CI pipeline, or as a separate validation step?

---

## References
- [transport_layer_spec.md](transport_layer_spec.md)
- [header_interfaces_spec.md](header_interfaces_spec.md)
- [json_rpc_handling_spec.md](json_rpc_handling_spec.md)
- [cpp_specifics_spec.md](cpp_specifics_spec.md)

---

## Change History
- 2026-06-16 - Added/updated in this PR.
- 2026-04-17 - Restructured to match spec template.

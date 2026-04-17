# Openspec Coverage Report

**Total Score: 67.74 / 100**

---

## Code to Spec Coverage: 15.74 / 40
  - Reference Coverage:  0.59 / 20
  - Spec Existence:      10.00 / 10
  - Spec Completeness:   5.00 / 5  (5/5 specs have all required sections)
  - No Orphaned Code:    0.15 / 5

### Spec Completeness Detail
  - ✓ `header_interfaces_spec`: all required sections present
  - ✓ `transport_layer_spec`: all required sections present
  - ✓ `cpp_specifics_spec`: all required sections present
  - ✓ `transport_recommendations_spec`: all required sections present
  - ✓ `json_rpc_handling_spec`: all required sections present

## Architecture HLA Specification: 10 / 10
  - Presence of HLA Spec:             3 / 3
  - Clarity of Architecture Diagrams: 3 / 3
  - Component/Module Mapping:         2 / 2
  - Traceability to Code:             2 / 2

## Performance Specification: 6 / 10
  - Presence of Performance Spec:  3 / 3
  - Defined Performance Metrics:   3 / 3
  - Test Coverage for Performance: 0 / 2
  - Results & Validation:          0 / 2

## External Interface Specification: 10 / 10
  - Presence of Interface Spec:  3 / 3
  - Defined Inputs/Outputs:      3 / 3
  - Documentation Completeness:  2 / 2
  - Validation/Examples:         2 / 2

## Security Specification: 8 / 10
  - Presence of Security Spec: 3 / 3
  - Threat Model/Analysis:     3 / 3
  - Security Requirements:     2 / 2
  - Validation/Testing:        0 / 2

## Versioning & Compatibility: 10 / 10
  - Presence of Versioning Spec:    3 / 3
  - Versioning Scheme Defined:      3 / 3
  - Backward/Forward Compatibility: 2 / 2
  - Migration/Upgrade Path:         2 / 2

## Conformance Testing Automation and Validation: 8 / 10
  - Presence of Conformance Tests: 3 / 3
  - Test Coverage:                 3 / 3
  - Test Documentation:            2 / 2
  - Validation Results:            0 / 2

---

## Orphaned Code Methods (not covered by any spec) — 33 total
- `src/gateway.cpp`: `onConnectionChange`
- `src/gateway.cpp`: `request`
- `src/gateway.cpp`: `lock`
- `src/gateway.cpp`: `lck`
- `src/helpers_impl.h`: `lock`
- `src/transport.cpp`: `mapError`
- `src/transport.cpp`: `lock`
- `src/transport.h`: `getResponseHeader`
- `src/transport.h`: `onClose`
- `src/transport.h`: `start`
- `src/transport.h`: `onOpen`
- `src/transport.h`: `send`
- `src/transport.h`: `onFail`
- `src/transport.h`: `getNextMessageID`
- `src/transport.h`: `~Transport`
- `src/transport.h`: `disconnect`
- `src/transport.h`: `setLogging`
- `src/transport.h`: `stopMessageWorker`
- `src/transport.h`: `processQueuedMessages`
- `src/transport.h`: `startMessageWorker`
- `include/firebolt/types.h`: `bool`
- `include/firebolt/types.h`: `has_value`
- `include/firebolt/gateway.h`: `~IGateway`
- `include/firebolt/logger.h`: `isLogLevelEnabled`
- `include/firebolt/logger.h`: `setFormat`
- `include/firebolt/logger.h`: `setLogLevel`
- `include/firebolt/helpers.h`: `unsubscribe`
- `include/firebolt/helpers.h`: `unsubscribeAll`
- `include/firebolt/json_types.h`: `nlohmann::json::type_error::create`
- `test/UnitTestsMain.cpp`: `RUN_ALL_TESTS`
- `test/unit/transportTest.cpp`: `promiseSet`
- `test/unit/helperTest.cpp`: `fromJson`
- `test/unit/helperTest.cpp`: `value`

# Openspec Coverage Report

**Total Score: 78.67 / 100**

---

## Code to Spec Coverage: 26.67 / 40
  - Reference Coverage:  10.00 / 20
    - Covered via spec 'Covered Code' sections: 17 method(s)
    - Additionally covered via '// Spec:' comments: 0 method(s)
  - Spec Existence:      10.00 / 10
  - Spec Completeness:   4.17 / 5  (5/6 specs have all required sections)
  - No Orphaned Code:    2.50 / 5

### Spec Completeness Detail
  - ✓ `openspec/specs/header_interfaces_spec.md`: all required sections present
  - ✓ `openspec/specs/transport_layer_spec.md`: all required sections present
  - ✓ `openspec/specs/cpp_specifics_spec.md`: all required sections present
  - ✓ `openspec/specs/transport_recommendations_spec.md`: all required sections present
  - ✓ `openspec/specs/json_rpc_handling_spec.md`: all required sections present
  - ✗ `openspec/changes/archive/2026-03-16-add-header-support/specs/header_support_spec.md`: missing: overview, description

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

## Orphaned Code Methods (not covered by any spec) — 17 total
- `src/gateway.cpp`: `lock`
- `src/gateway.cpp`: `lck`
- `src/gateway.cpp`: `onConnectionChange`
- `src/helpers_impl.h`: `lock`
- `src/transport.cpp`: `lock`
- `src/transport.cpp`: `mapError`
- `src/transport.h`: `stopMessageWorker`
- `src/transport.h`: `start`
- `src/transport.h`: `~Transport`
- `include/firebolt/types.h`: `has_value`
- `include/firebolt/types.h`: `bool`
- `include/firebolt/gateway.h`: `~IGateway`
- `include/firebolt/json_types.h`: `nlohmann::json::type_error::create`
- `test/UnitTestsMain.cpp`: `RUN_ALL_TESTS`
- `test/unit/transportTest.cpp`: `promiseSet`
- `test/unit/helperTest.cpp`: `value`
- `test/unit/helperTest.cpp`: `fromJson`

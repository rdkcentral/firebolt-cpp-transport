# Openspec Coverage Report (Spec-Driven Mapping)

**Code to Spec Coverage:** 10.74/40
  - Reference Coverage: 0.59/20
  - Spec Existence: 10.00/10
  - Spec Completeness: 0.00/5
  - No Orphaned Code: 0.15/5

**Architecture HLA Specification:** 0/10
**Performance Specification:** 0/10
**External Interface Specification:** 0/10
**Security Specification:** 0/10
**Versioning & Compatibility:** 0/10
**Conformance Testing Automation and Validation:** 0/10

## TOTAL SCORE: 10.74/100

## Orphaned Code Methods (not covered by any spec):
- `src/gateway.cpp`: `onConnectionChange`
- `src/gateway.cpp`: `request`
- `src/gateway.cpp`: `lock`
- `src/gateway.cpp`: `lck`
- `src/helpers_impl.h`: `lock`
- `src/transport.cpp`: `mapError`
- `src/transport.cpp`: `lock`
- `src/transport.h`: `~Transport`
- `src/transport.h`: `onClose`
- `src/transport.h`: `getNextMessageID`
- `src/transport.h`: `disconnect`
- `src/transport.h`: `startMessageWorker`
- `src/transport.h`: `setLogging`
- `src/transport.h`: `onFail`
- `src/transport.h`: `getResponseHeader`
- `src/transport.h`: `send`
- `src/transport.h`: `onOpen`
- `src/transport.h`: `stopMessageWorker`
- `src/transport.h`: `start`
- `src/transport.h`: `processQueuedMessages`
- `include/firebolt/types.h`: `bool`
- `include/firebolt/types.h`: `has_value`
- `include/firebolt/gateway.h`: `~IGateway`
- `include/firebolt/logger.h`: `setLogLevel`
- `include/firebolt/logger.h`: `setFormat`
- `include/firebolt/logger.h`: `isLogLevelEnabled`
- `include/firebolt/helpers.h`: `unsubscribeAll`
- `include/firebolt/helpers.h`: `unsubscribe`
- `include/firebolt/json_types.h`: `nlohmann::json::type_error::create`
- `test/UnitTestsMain.cpp`: `RUN_ALL_TESTS`
- `test/unit/transportTest.cpp`: `promiseSet`
- `test/unit/helperTest.cpp`: `fromJson`
- `test/unit/helperTest.cpp`: `value`

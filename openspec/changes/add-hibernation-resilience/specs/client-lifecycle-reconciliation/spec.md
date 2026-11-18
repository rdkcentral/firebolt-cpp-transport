# Client Lifecycle Reconciliation Specification

## Overview
Defines how the Firebolt C++ client (`firebolt-cpp-client`) keeps an application's view of its lifecycle state consistent across a process stop and resume, without application code. It relies on the transport resume callback specified in `hibernation-resilience`.

## Description
The client is the only layer that understands Firebolt lifecycle. App Gateway **drops** `Lifecycle2.onStateChanged` events while the container is passivated, so without this capability the app never learns about transitions that happened while it was stopped. On resume, the client checks the authoritative state and, if necessary, delivers one corrective notification, so the app needs no reconciliation logic of its own (epic AC1/AC6). This is phase 2 of the change and depends on the transport resume callback and the post-resume grace period.

## ADDED Requirements

### Requirement: Client registers for transport resume while connected
`IFireboltAccessor::Connect` SHALL register a transport resume listener, and `IFireboltAccessor::Disconnect` SHALL clear it.

#### Scenario: Listener registered on connect
- **WHEN** the app calls `Connect`
- **THEN** the client calls `IGateway::setResumeListener` with a non-null listener

#### Scenario: Listener cleared on disconnect
- **WHEN** the app calls `Disconnect`
- **THEN** the client calls `IGateway::setResumeListener(nullptr)`

### Requirement: Lifecycle state reconciled on resume
On a transport resume callback, the client SHALL call `Lifecycle2.state`. If the app has at least one `onStateChanged` subscription and the returned state differs from the last state delivered to the app, the client SHALL invoke each subscriber once with a vector containing exactly one `StateChange{oldState: last delivered, newState: current}`.

#### Scenario: State changed while stopped
- **WHEN** the last delivered state is `ACTIVE`, a resume occurs, and `Lifecycle2.state` returns `SUSPENDED`
- **THEN** each `onStateChanged` subscriber receives exactly one change `{ACTIVE → SUSPENDED}`

#### Scenario: State unchanged while stopped
- **WHEN** the last delivered state is `ACTIVE`, a resume occurs, and `Lifecycle2.state` returns `ACTIVE`
- **THEN** no notification is delivered

#### Scenario: No subscribers
- **WHEN** the app has no `onStateChanged` subscription and a resume occurs
- **THEN** no notification is delivered, and the reconciliation request may be skipped

#### Scenario: Last delivered state unknown
- **WHEN** no `onStateChanged` change has yet been delivered to the app and a resume occurs
- **THEN** the client makes no `Lifecycle2.state` request and delivers no notification

#### Scenario: State query fails
- **WHEN** a resume occurs and `Lifecycle2.state` returns an error
- **THEN** no notification is delivered, the failure is logged, and the last delivered state is unchanged

### Requirement: Duplicate lifecycle notifications suppressed
A `Lifecycle2.onStateChanged` event whose final `newState` equals the last state delivered to that subscriber, and whose first `oldState` differs from it, SHALL NOT be delivered to that subscriber. An event that starts and ends at the last delivered state (a round trip) SHALL be delivered. This is defensive: App Gateway is expected to drop events during passivation, but a late or re-sent event must not reach the app twice.

#### Scenario: Held event duplicates synthesized change
- **WHEN** the client delivered a synthesized `{ACTIVE → SUSPENDED}` and a real `{ACTIVE → SUSPENDED}` event then arrives
- **THEN** each subscriber receives the change only once

#### Scenario: Round trip from the current state
- **WHEN** the last delivered state is `ACTIVE` and an event `[{ACTIVE → PAUSED}, {PAUSED → ACTIVE}]` arrives
- **THEN** the event is delivered

## Architecture / Design

```text
 transport ResumeCallback ─▶ FireboltAccessorImpl ─▶ LifecycleImpl::reconcile()
                                                         │ Lifecycle2.state
                                                         ▼
                                       compare with lastDelivered ─▶ fan-out to app subscribers
```

- `LifecycleImpl` wraps each app callback; the wrapper tracks that subscriber's last delivered `newState` (for duplicate suppression) and the module-wide last delivered state (the `oldState` of a synthesized change).
- The synthesized change is delivered with `SubscriptionManager::dispatch` (transport `IHelper::dispatchLocal`), so it passes through the same wrappers as wire events and the client needs no locking.
- `FireboltAccessorImpl` lives in `src/firebolt_impl.h` and takes `IGateway&` and `IHelper&`, so the wiring is unit-testable; `IFireboltAccessor::Instance()` passes the transport singletons.
- The listener runs on the transport notification worker thread (see `hibernation-resilience`), so the reconciliation request may block.
- See `design.md` D4.

## External Interfaces
- No public API change in the client. `ILifecycle` is unchanged.
- Uses the transport's `IGateway::setResumeListener` and `SubscriptionManager::dispatch`.
- Requires `IGateway` to be exported from the transport shared library (the client mocks it in unit tests).

## Performance
- One `Lifecycle2.state` request per resume, and only when the app has subscribers.

## Security
_Not applicable — no new inputs or data exposure._

## Versioning & Compatibility
- Requires the transport version that introduces `IGateway::setResumeListener`. The client's transport dependency pin must be raised accordingly.
- App-visible behavior differs only after a resume: at most one extra or corrective `onStateChanged` notification.

## Conformance Testing & Validation
- **Approach:** test-first (design D0). Write each test against the mock helper first; it fails until `reconcile` and the listener wiring exist.
- **Unit (`firebolt-cpp-client/test/unit/lifecycleTest.cpp`):** use the mock helper to drive `reconcile()` with each `Lifecycle2.state` response and assert delivered changes, including duplicate suppression and the error path.
- **Unit (`firebolt-cpp-client/test/unit/fireboltTest.cpp`):** `FireboltAccessorImpl` with a mock `IGateway`: listener set before `connect`, cleared before `disconnect`, and invoking it reconciles the lifecycle.
- **Test fake (`firebolt-cpp-client/test/unit/fake_subscriptions.h`):** stores subscriptions like `HelperImpl` and delivers wire (`emit`) and local (`dispatchLocal`) events.

## Covered Code
- firebolt-cpp-client/src/firebolt_impl.h:
    - FireboltAccessorImpl::FireboltAccessorImpl
    - FireboltAccessorImpl::Connect
    - FireboltAccessorImpl::Disconnect
- firebolt-cpp-client/src/firebolt.cpp:
    - IFireboltAccessor::Instance
- firebolt-cpp-client/src/lifecycle_impl.h:
    - LifecycleImpl
- firebolt-cpp-client/src/lifecycle_impl.cpp:
    - LifecycleImpl::subscribeOnStateChanged
    - LifecycleImpl::unsubscribe
    - LifecycleImpl::unsubscribeAll
    - LifecycleImpl::reconcile (new)
- firebolt-cpp-client/include/firebolt/lifecycle.h:
    - ILifecycle (unchanged; referenced)

---

## Open Queries
- Should reconciliation be limited to transitions involving `HIBERNATED` and `SUSPENDED`?
- Does App Gateway also drop non-lifecycle subscribed events during passivation? That would be an app-visible gap, outside this capability.
- `firebolt-cpp-client` has no OpenSpec setup. Should this spec move there once `openspec init` is run in that repo?

Resolved (2026-10-21):
- App Gateway drops lifecycle events during passivation, so this capability is required.

## References
- `specs/hibernation-resilience/spec.md` in this change
- `design.md` D4
- Hibernation epic AC1, AC6

## Change History
- 2026-10-21 - Copilot - Initial draft
- 2026-10-21 - Copilot - Made the capability required (App Gateway drops lifecycle events during passivation); adopted test-first delivery
- 2026-10-21 - Copilot - Implemented test-first; added unknown-last-state and round-trip scenarios; documented dispatch path and injectable accessor

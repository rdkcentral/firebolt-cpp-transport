# Hibernation Resilience Specification

## Overview
Defines how the transport behaves when its host process is stopped and later resumed by the platform, as with a Dobby container pause or memcr hibernation. Timeouts count only active time, and resumes are detected and reported through a lifecycle-agnostic callback.

## Description
Containerized Firebolt applications can be stopped at any time with no in-process notification. While they are stopped, `steady_clock` keeps advancing. Without this capability, every request pending at the stop fails with `Error::Timedout` on the first watchdog tick after the resume, even though App Gateway holds and later delivers the response. This capability makes timeouts measure only time when the process was running, and it reports resumes so higher layers, such as the client library, can reconcile state. The transport stays unaware of Firebolt lifecycle semantics.

## ADDED Requirements

### Requirement: Request timeouts count active time only
The transport SHALL fail a pending request with `Error::Timedout` only when the active time elapsed since the request was queued exceeds `Config::waitTime_ms`. Active time SHALL exclude any interval between consecutive clock samples that exceeds the gap threshold.

#### Scenario: Stop longer than waitTime_ms does not fail a pending request
- **WHEN** a request is pending, the clock jumps forward by 10 × `waitTime_ms`, and the watchdog runs
- **THEN** the request is not failed, and its future is still unresolved

#### Scenario: Response delivered after resume fulfills the request
- **WHEN** a request is pending across a stop and the gateway sends its response after the resume
- **THEN** the request's future resolves with that response, not with `Error::Timedout`

#### Scenario: Normal timeout is preserved
- **WHEN** a request is pending, no stop occurs, and the gateway never responds
- **THEN** the request fails with `Error::Timedout` after `waitTime_ms` of active time plus at most one watchdog interval

#### Scenario: Timeout resumes counting after a stop
- **WHEN** a request was pending for 1000 ms of active time, a stop occurs, and the gateway never responds
- **THEN** the request fails with `Error::Timedout` after a further `waitTime_ms` − 1000 ms of active time, plus at most one watchdog interval

### Requirement: Gap threshold distinguishes a stop from jitter
The transport SHALL treat an interval between consecutive clock samples as stopped time only if it exceeds the effective gap threshold, `max(Config::stopGapThreshold_ms, 4 × Config::watchdogCycle_ms)`. Shorter intervals SHALL count fully as active time. If the configured value is below `4 × watchdogCycle_ms`, the transport SHALL log a warning and use the clamped value.

#### Scenario: Jitter below threshold counts as active time
- **WHEN** consecutive samples are 1500 ms apart with default config (`stopGapThreshold_ms = 2000`)
- **THEN** all 1500 ms count as active time

#### Scenario: Gap above threshold counts as stopped time
- **WHEN** consecutive samples are 2500 ms apart with default config
- **THEN** none of the 2500 ms counts as active time, and the 2500 ms is recorded as stopped time

#### Scenario: Configured threshold is honored
- **WHEN** `stopGapThreshold_ms = 10000`, `watchdogCycle_ms = 500`, and consecutive samples are 5000 ms apart
- **THEN** all 5000 ms count as active time

#### Scenario: Configured threshold is clamped
- **WHEN** `stopGapThreshold_ms = 100` and `watchdogCycle_ms = 500`
- **THEN** the effective threshold is 2000 ms, and a warning is logged

### Requirement: Post-resume grace period tolerates traffic held after thaw
After a resume is detected, the transport SHALL open a grace window lasting `Config::resumeGrace_ms` of active time. A request pending at the resume, or sent during the window, SHALL NOT fail with `Error::Timedout` before the window ends. An acknowledgement wait started during the window SHALL last until the window ends, and at least 50 ms of active time. Setting `resumeGrace_ms = 0` SHALL disable the grace period.

#### Scenario: Response held after thaw beyond waitTime_ms
- **WHEN** `waitTime_ms = 3000` and `resumeGrace_ms = 5000`, a request is pending across a stop, and the gateway delivers the response 4000 ms of active time after the resume
- **THEN** the request resolves with that response

#### Scenario: Request sent just after thaw is held
- **WHEN** `waitTime_ms = 3000` and `resumeGrace_ms = 5000`, a request is sent 100 ms after a resume, and its response arrives 4500 ms after the resume
- **THEN** the request resolves with that response

#### Scenario: Genuine failure is still reported after the grace period
- **WHEN** a request is pending across a stop and the gateway never responds
- **THEN** the request fails with `Error::Timedout` once active time exceeds `max(enqueue + waitTime_ms, resume + resumeGrace_ms)`, plus at most one watchdog interval

#### Scenario: Late acknowledgement during the grace window
- **WHEN** `subscribe` is called during the grace window and the acknowledgement arrives 1000 ms of active time later, but before the window ends
- **THEN** `subscribe` returns `Error::None`

#### Scenario: Grace disabled
- **WHEN** `resumeGrace_ms = 0`, a request is pending across a stop, and no response arrives
- **THEN** the request fails after `waitTime_ms` of active time, as in the requirement for active-time request timeouts

### Requirement: Subscription acknowledgement waits count active time only
`IGateway::subscribe` and `IGateway::unsubscribe` SHALL wait for the gateway acknowledgement until it arrives or 50 ms of active time has elapsed. Stopped time SHALL NOT count toward that wait.

#### Scenario: Stop during subscribe acknowledgement wait
- **WHEN** `subscribe` is waiting for its acknowledgement, the process is stopped for longer than 50 ms, and the acknowledgement arrives within 50 ms of active time after the resume
- **THEN** `subscribe` returns `Error::None`, and the subscription stays registered

#### Scenario: Missing acknowledgement still times out
- **WHEN** `subscribe` is waiting and no acknowledgement arrives, with no stop
- **THEN** `subscribe` returns `Error::Timedout` after about 50 ms, and the local subscription is removed (existing behavior)

#### Scenario: Missing unsubscribe acknowledgement
- **WHEN** `unsubscribe` is waiting and no acknowledgement arrives within 50 ms of active time
- **THEN** `unsubscribe` returns `Error::None`, because the local subscriber was already removed (existing behavior)

### Requirement: Resume is reported through a lifecycle-agnostic callback
The transport SHALL expose `IGateway::setResumeListener(ResumeCallback)`. When stopped time is detected while connected, the transport SHALL invoke the listener once with the total stopped duration accumulated since the last dispatch. The listener SHALL run on the notification worker thread, never on the watchdog thread. Setting a listener SHALL replace the previous one, and setting `nullptr` SHALL clear it.

#### Scenario: Listener notified after a stop
- **WHEN** a listener is set, the transport is connected, and the clock jumps forward beyond the gap threshold
- **THEN** the listener is invoked exactly once with `stoppedFor` greater than or equal to the jump

#### Scenario: No notification without a stop
- **WHEN** a listener is set and the transport runs normally for 10 watchdog intervals
- **THEN** the listener is not invoked

#### Scenario: Multiple gaps before dispatch are coalesced
- **WHEN** two gaps are detected before the resume item is dispatched
- **THEN** the listener is invoked once with the sum of both gaps

#### Scenario: Listener may issue requests
- **WHEN** the listener calls `IGateway::request` and waits on the returned future
- **THEN** the request completes or times out normally, and the watchdog keeps running

#### Scenario: Cleared listener is not invoked
- **WHEN** `setResumeListener(nullptr)` is called and then a stop occurs
- **THEN** no callback is invoked

### Requirement: Local dispatch to an owner's subscribers
`IHelper::dispatchLocal(owner, eventName, payload)` SHALL deliver `payload` to every subscription of `eventName` registered by `owner`, as if it had arrived from the gateway, without sending anything on the wire. Callbacks SHALL run on the calling thread, outside the helper's lock. `SubscriptionManager::dispatch(eventName, payload)` SHALL forward to it with its owner. The `IHelper` default SHALL return `Error::General`.

#### Scenario: Only the owner's subscribers of the event receive the payload
- **WHEN** owner A has two subscriptions to event E and one to event F, owner B has one to E, and `dispatchLocal(A, E, payload)` is called
- **THEN** only A's two E subscriptions receive the payload

#### Scenario: Subscription removed during dispatch is dropped
- **WHEN** a callback invoked by `dispatchLocal` unsubscribes another matching subscription that has not yet been invoked
- **THEN** the removed subscription is not invoked

#### Scenario: Callback may unsubscribe itself
- **WHEN** a callback invoked by `dispatchLocal` unsubscribes itself
- **THEN** no deadlock occurs and the dispatch returns success

#### Scenario: Default implementation
- **WHEN** an `IHelper` implementation does not override `dispatchLocal`
- **THEN** the call returns `Error::General`

## Architecture / Design

```text
 steady_clock ─▶ ActiveClock ──active ms──▶ Client::checkPromises (watchdog)
                     │          └──────────▶ subscribe/unsubscribe ACK wait
                     └─stopped ms─▶ watchdog ─▶ Server notification queue ─▶ ResumeCallback
```

- `ActiveClock` (new, `src/active_clock.h`) accumulates active time and records gaps. Its time source can be injected under `FIREBOLT_TRANSPORT_TESTING`.
- `Client::Caller` stores the active time at enqueue instead of a `steady_clock` timestamp.
- The watchdog samples `ActiveClock` every tick and posts any accumulated gap to the notification worker.
- See `design.md` D1–D3.

## External Interfaces
- `include/firebolt/gateway.h`:
  - `using ResumeCallback = std::function<void(std::chrono::milliseconds stoppedFor)>;`
  - `virtual void IGateway::setResumeListener(ResumeCallback listener)`: non-pure, default no-op.
- `include/firebolt/config.h.in` (fields appended at the end of `Config`):
  - `unsigned stopGapThreshold_ms = 2000;` — the minimum clock gap treated as a process stop, clamped to `≥ 4 × watchdogCycle_ms`.
  - `unsigned resumeGrace_ms = 5000;` — active time after a resume during which held traffic is tolerated; 0 disables it.
- `include/firebolt/helpers.h`:
  - `virtual Result<void> IHelper::dispatchLocal(void* owner, const std::string& eventName, const nlohmann::json& payload)`: non-pure, default returns `Error::General`.
  - `Result<void> SubscriptionManager::dispatch(const std::string& eventName, const nlohmann::json& payload)`.
- No wire protocol changes.

## Performance
- Clock sampling adds one mutex-protected `steady_clock::now()` per watchdog tick and per 10 ms acknowledgement-wait slice. This cost is negligible.
- Subscribe and unsubscribe latency is unchanged when the acknowledgement arrives promptly.

## Security
_Not applicable — no new inputs, privileges, or data exposure; the stopped duration is logged only at NOTICE level._

## Versioning & Compatibility
- Adding virtual methods to `IGateway` and `IHelper`, a method to `SubscriptionManager`, and fields to `Config` breaks ABI and requires a minor-version bump.
- Source-compatible: the default implementation means existing `IGateway` subclasses and mocks compile unchanged.
- The `waitTime_ms` semantics are unchanged for a process that is not stopped.

## Conformance Testing & Validation
- **Approach:** test-first (design D0). Every scenario gets a failing test before it is implemented.
- **Unit (`test/unit/`):** `ActiveClock` with an injected time source covers jitter vs. gap, the configured and clamped threshold, accumulation, coalescing, and grace-window bounds. `Client::checkPromises` with an injected clock covers survival across a time jump, the normal timeout, the timeout resuming after a stop, and grace-period extension and expiry. The resume listener covers dispatch, no dispatch without a gap, clearing, and issuing a request from the listener.
- **Gateway (`test/unit/gatewayTest.cpp`):** the in-process fake server can be scripted to hold responses and acknowledgements and release them after a set amount of active time. This covers late acknowledgements within and outside the grace window.
- **Helper (`test/unit/helperTest.cpp`):** `dispatchLocal` owner/event filtering, no subscribers, unsubscribed, self-unsubscribe, removal mid-dispatch, the `IHelper` default, and `SubscriptionManager::dispatch` forwarding.
- **Freeze harness (`test/integration/`):** a forked child runs the client while the parent runs the fake server and sends `SIGSTOP`, waits longer than `waitTime_ms`, then sends `SIGCONT`. The child must receive the held response and one resume callback.
- Every scenario above maps to at least one test.

## Covered Code
- src/active_clock.h:
    - ActiveClock::sample
    - ActiveClock::activeNow
    - ActiveClock::takeStoppedTime
    - ActiveClock::graceRemaining
- src/gateway.cpp:
    - Client::Caller
    - Client::checkPromises
    - Client::request
    - GatewayImpl::connect (watchdog thread loop)
    - GatewayImpl::subscribe
    - GatewayImpl::unsubscribe
    - GatewayImpl::setResumeListener
    - Server::processQueuedNotifications
- include/firebolt/gateway.h:
    - IGateway::setResumeListener
    - ResumeCallback
- include/firebolt/helpers.h:
    - IHelper::dispatchLocal
    - SubscriptionManager::dispatch
- src/helpers_impl.h:
    - HelperImpl::dispatchLocal
    - HelperImpl::subscribe
- src/helpers_impl.cpp:
    - SubscriptionManager::dispatch
- include/firebolt/config.h.in:
    - Config::stopGapThreshold_ms
    - Config::resumeGrace_ms

---

## Open Queries
- What default `resumeGrace_ms` matches App Gateway's real post-thaw hold time? 5000 ms is a placeholder until device measurements exist.
- Should the grace period also apply to requests sent shortly *before* a stop that is detected later? It currently does, because any request still pending at the resume is covered.

## References
- `design.md` in this change
- `openspec/specs/transport_layer_spec.md`
- Dobby hibernation via memcr: https://github.com/rdkcentral/Dobby, https://github.com/LibertyGlobal/memcr
- Hibernation epic AC2, AC5, AC6

## Change History
- 2026-10-21 - Copilot - Initial draft
- 2026-10-21 - Copilot - Made the gap threshold configurable; added the post-resume grace period because App Gateway may keep holding traffic after the thaw; adopted test-first delivery
- 2026-10-21 - Copilot - Added local dispatch (`IHelper::dispatchLocal`, `SubscriptionManager::dispatch`) so the client can fan out a reconciled lifecycle change without its own locking (user decision)

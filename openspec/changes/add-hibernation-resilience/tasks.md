**TDD is the first-order rule (design D0).** Every behavior task is a 🔴 red/🟢 green pair:

- Commit the failing test first, and confirm it fails for the expected reason.
- Then write the minimum code that makes it pass.
- Then refactor with the full `utApp` suite green.

No production code is written without a failing test that demands it. Unit tests use only the injected clock and a scripted fake gateway, never wall-clock sleeps, so they stay deterministic. Any later defect is first reproduced as a failing unit test.

## 0. Test seams (no behavior change)

- [x] 0.1 Add `src/active_clock.h` with an injectable time source that currently passes `steady_clock` through. Add a `FIREBOLT_TRANSPORT_TESTING` hook on `GatewayImpl` to install a fake clock (`testSetGatewayTimeSource`) and run one watchdog pass (`testRunGatewayWatchdogTick`). Add `ManualClock`/`JumpClock` test helpers (`test/unit/testClock.h`). Existing tests must stay green — spec: hibernation-resilience § Architecture / Design
- [x] 0.2 Add the `test/unit/scriptedGateway.h` fake App Gateway (ephemeral loopback port) with scripted controls: `holdMethod()`, `release()`/`releaseAll()`, `waitForRequest(method, n, timeout)` and `notify()`. Self-test in `hibernationTest.cpp` (`HibernationUTest`); existing tests must stay green — spec: hibernation-resilience § Conformance Testing & Validation

## 1. Gap detection and configuration

- [x] 1.1 🔴 Write `activeClockTest.cpp`: jitter below the threshold counts as active; a gap counts as stopped; `takeStoppedTime` coalesces and resets; a configured threshold is honored; a too-small threshold is clamped to `4 × watchdogCycle_ms` — spec: hibernation-resilience § Requirement: Gap threshold distinguishes a stop from jitter
- [x] 1.2 🟢 Implement `ActiveClock` accounting, and add `Config::stopGapThreshold_ms` (appended, default 2000) with clamp and warning — spec: hibernation-resilience § Requirement: Gap threshold distinguishes a stop from jitter; § External Interfaces

## 2. Request timeouts on active time

- [x] 2.1 🔴 Write tests: a pending request survives a jump of 10 × `waitTime_ms`; a response released after the jump fulfills it; the normal timeout still fires; the timeout resumes counting after a stop. The jump tests must fail against current code with `Timedout` — spec: hibernation-resilience § Requirement: Request timeouts count active time only
- [x] 2.2 🟢 Store the active time at enqueue in `Client::Caller`; base `checkPromises` on active elapsed time; sample the clock on each watchdog tick and on `request` entry — spec: hibernation-resilience § Requirement: Request timeouts count active time only

## 3. Acknowledgement waits on active time

- [x] 3.1 🔴 Write tests: an acknowledgement held across a clock jump and released within 50 ms of active time makes `subscribe` and `unsubscribe` return `None`; a missing `subscribe` acknowledgement still returns `Timedout` and removes the local subscription; a missing `unsubscribe` acknowledgement still returns `None` (existing behavior) — spec: hibernation-resilience § Requirement: Subscription acknowledgement waits count active time only
- [x] 3.2 🟢 Replace `wait_for(50ms)` in `GatewayImpl::subscribe` and `unsubscribe` with a 10 ms-slice loop measured in active time — spec: hibernation-resilience § Requirement: Subscription acknowledgement waits count active time only

## 4. Post-resume grace period

- [x] 4.1 🔴 Write tests with `waitTime_ms = 3000` and `resumeGrace_ms = 5000`: a response held 4000 ms after the resume is fulfilled; a request sent just after the thaw and answered at 4500 ms is fulfilled; a genuine non-response fails after `max(enqueue + wait, resume + grace)`; `resumeGrace_ms = 0` restores strict behavior — spec: hibernation-resilience § Requirement: Post-resume grace period tolerates traffic held after thaw
- [x] 4.2 🟢 Add `Config::resumeGrace_ms` (appended, default 5000) and `ActiveClock::graceRemaining`, and extend `checkPromises` deadlines inside the grace window — spec: hibernation-resilience § Requirement: Post-resume grace period tolerates traffic held after thaw
- [x] 4.3 🔴→🟢 Write a test where `subscribe` during the grace window gets its acknowledgement 1000 ms later and returns `None`; extend the acknowledgement wait to `max(50 ms, graceRemaining)` — spec: hibernation-resilience § Scenario: Late acknowledgement during the grace window

## 5. Resume listener

- [x] 5.1 🔴 Write tests: the listener is called once after a gap; not called without a gap; two gaps are coalesced; a cleared listener is not called; the listener runs off the watchdog thread and can make a blocking `request` (answered by the scripted server) without stalling timeouts — spec: hibernation-resilience § Requirement: Resume is reported through a lifecycle-agnostic callback
- [x] 5.2 🟢 Add `ResumeCallback` and a non-pure virtual `IGateway::setResumeListener`; implement `GatewayImpl::setResumeListener`; have the watchdog drain stopped time and enqueue an internal resume item on the `Server` notification queue; log at NOTICE — spec: hibernation-resilience § Requirement: Resume is reported through a lifecycle-agnostic callback; § External Interfaces
- [x] 5.3 🔴→🟢 Write `helperTest.cpp` tests, then add non-pure `IHelper::dispatchLocal`, `SubscriptionManager::dispatch` and `HelperImpl::dispatchLocal` (owner/event filter, callbacks outside the lock, removed subscriptions dropped) — spec: hibernation-resilience § Requirement: Local dispatch to an owner's subscribers

## 6. End-to-end freeze harness

- [x] 6.1 Write a `test/integration/` forked-child test: the parent runs the fake server; the child sends a request; the parent sends `SIGSTOP`, sleeps longer than `waitTime_ms`, sends `SIGCONT`, then holds the reply for a while (within grace) before responding. Assert success and one resume callback through a pipe. Expected to pass on arrival; if it fails, add the missing unit test before fixing — spec: hibernation-resilience § Conformance Testing & Validation
- [x] 6.2 Wire the harness into CMake/CTest and CI, and document how to run it locally — spec: hibernation-resilience § Conformance Testing & Validation

## 7. Transport release

- [x] 7.1 Bump the minor version (`IGateway` vtable and `Config` layout); update CHANGELOG and README/docs: active-time `waitTime_ms`, `stopGapThreshold_ms`, `resumeGrace_ms`, the resume listener — spec: hibernation-resilience § Versioning & Compatibility

## 8. Client lifecycle reconciliation (firebolt-cpp-client)

- [x] 8.1 🔴 Write tests in `lifecycleTest.cpp` (mock helper): `Connect` registers and `Disconnect` clears the resume listener; on resume with a changed state each subscriber gets exactly one `{last → current}`; no notification when the state is unchanged, with no subscribers, or on error — spec: client-lifecycle-reconciliation § Requirement: Client registers for transport resume while connected; § Requirement: Lifecycle state reconciled on resume
- [x] 8.2 🟢 Raise the transport dependency; wire the listener in `FireboltAccessorImpl::Connect`/`Disconnect`; implement `LifecycleImpl::reconcile` with last-delivered-state tracking, delivering through `SubscriptionManager::dispatch` — spec: client-lifecycle-reconciliation § Requirement: Lifecycle state reconciled on resume
- [x] 8.3 🔴→🟢 Write a test where a real event duplicating the synthesized state is delivered once; implement suppression — spec: client-lifecycle-reconciliation § Requirement: Duplicate lifecycle notifications suppressed

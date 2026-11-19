## Context

The Firebolt C++ libraries run inside a process in a Dobby (OCI) container. Dobby can stop that process in two ways:

| Mechanism | How the process is stopped | Signal visible in the process |
|---|---|---|
| `pauseContainer` / `resumeContainer` | cgroup freezer | none |
| `hibernateContainer` / `wakeupContainer` | memcr: `PTRACE_SEIZE`, memory dump, `MADV_DONTNEED`, restore | none |

In both cases, every thread stops at once and later resumes. No code can run before the stop. After the resume, the process can only observe that **monotonic time jumped**: `CLOCK_MONOTONIC` and `steady_clock` keep advancing while a container is frozen. `CLOCK_BOOTTIME` differs from them only during system suspend, so it cannot detect a container stop.

App Gateway is assumed to keep the WebSocket open and to hold traffic while the container is stopped (epic AC4, out of scope here). Responses to requests sent before the stop therefore arrive *after* the resume.

Current transport behavior (`src/gateway.cpp`):

- `Client::Caller::timestamp` is `steady_clock::now()` at enqueue. The watchdog thread runs `checkPromises()` every `watchdogCycle_ms` (500 ms) and fails callers older than `waitTime_ms` (3000 ms).
- `GatewayImpl::subscribe` / `unsubscribe` block on `future.wait_for(50ms)` for the `listen` acknowledgement.

After any stop longer than about 3 s, the first watchdog tick fails every pending request, and a stop during an acknowledgement wait fails that subscription. Responses that arrive later have no matching caller, so the transport logs "No receiver for a message" and drops them.

The client (`firebolt-cpp-client`) is the only component that understands lifecycle. Transport must remain lifecycle-agnostic.

## Goals / Non-Goals

**Goals:**
- Timeouts count only time when the process was running. A container stop of any length does not fail requests or acknowledgement waits on its own.
- Transport detects a resume after the fact and reports it through a lifecycle-agnostic callback.
- The client uses that callback to keep the app's view of its lifecycle consistent without app code (AC1/AC6).
- **Test-first (D0):** every behavior is driven by a failing deterministic, in-process test before implementation, without a device (AC5).

**Non-Goals:**
- AppGatewayResponder changes, reconnect or subscription replay, pre-stop detection, media, and on-device memcr validation. See the proposal's Non-goals.
- Changing `waitTime_ms` semantics for a process that is running normally.

## Decisions

### D0: Test-first delivery (first-order rule)

Test-driven development is the **primary constraint** of this change. It takes precedence over every other decision below: no production line is written without a failing test that requires it.

1. **Seams first.** Add the clock injection hook and scripted fake-gateway controls: hold responses, release them, delay acknowledgements. These seams change no behavior and keep existing tests green.
2. **Red.** Write a failing test for one spec scenario and commit it. For example, a pending request survives a 10 × `waitTime_ms` clock jump; today it fails with `Timedout`. The test must fail for the expected reason, not because of a compile or setup error.
3. **Green.** Make the smallest change in `gateway.cpp` or `active_clock.h` that passes the test.
4. **Refactor.** Do this only with the whole suite green.

Gate rules:
- Every `#### Scenario` in the specs maps to a named test before its implementation task starts.
- A task is not done until its tests pass in `utApp` together with the full existing suite.
- Unit tests use the injected clock and the scripted fake server only, with no wall-clock sleeps beyond the existing watchdog cadence. They must be deterministic, so flaky means broken.
- A bug found later, for example by the freeze harness (D5) or on a device, is first reproduced as a failing unit test and only then fixed.

### D1: Measure timeouts in active time

Add an internal `ActiveClock` in `src/active_clock.h`. It samples a monotonic time source and accumulates **active milliseconds**:

```text
delta = now_real - last_real
if delta <= gapThreshold:  active += delta          // normal progress
else:                      stopped += delta          // a stop: count none of it
                           pendingResumeGap += delta
last_real = now_real
```

- The `Caller` stores `activeAtEnqueue`. `checkPromises()` fails a request when `active - activeAtEnqueue > waitTime_ms`.
- **Gap threshold:** `Config::stopGapThreshold_ms`, default 2000. The effective value is `max(stopGapThreshold_ms, 4 × watchdogCycle_ms)`, and a warning is logged when the configured value is clamped. It must exceed the watchdog interval with margin so that scheduling jitter is not treated as a stop.
- **Sampling points:** the watchdog samples every `watchdogCycle_ms` while connected, and `request`, `subscribe` and `unsubscribe` also sample on entry. A resume is therefore detected on the first transport call after thawing, not up to one watchdog cycle later.
- **Time source injection:** `ActiveClock` takes a `std::function<steady_clock::time_point()>` and defaults to `steady_clock::now`. Under `FIREBOLT_TRANSPORT_TESTING`, a hook replaces it so tests can make time jump deterministically.

*Alternatives considered:*
- Compare against `CLOCK_BOOTTIME`: it does not detect cgroup or memcr stops.
- Read cgroup `cgroup.events`: memcr does not use it, the container may not have access, and the process can only read it after the stop anyway.
- Ask the client to pause and resume transport on lifecycle events: the pause event can arrive too late or not before the stop at all, and it couples transport correctness to lifecycle. This could be added later as an optimization; correctness does not depend on it.
- `CLOCK_PROCESS_CPUTIME_ID`: an idle process waiting for a response consumes no CPU, so timeouts would never fire.

### D2: Measure acknowledgement waits in active time

Replace `future.wait_for(50ms)` in `subscribe` and `unsubscribe` with a loop: `wait_for(10ms)` slices until the future is ready or the active elapsed time reaches 50 ms. A stop during the wait adds no active time.

### D3: Lifecycle-agnostic resume callback on `IGateway`

```cpp
using ResumeCallback = std::function<void(std::chrono::milliseconds stoppedFor)>;
virtual void setResumeListener(ResumeCallback listener) {}   // non-pure: no-op default
```

- **Non-pure with a no-op default:** existing `IGateway` implementations and mocks still compile. The vtable changes, so this is an ABI break and requires a minor-version bump.
- **One listener:** the client is the only consumer. Setting a new listener replaces the previous one; `nullptr` clears it.
- **Dispatch thread:** the watchdog drains `pendingResumeGap` and posts one resume item to the **notification worker**, the thread that already runs event callbacks. The listener may therefore issue requests and wait on them, the same rule that applies to event callbacks. It must not run on the watchdog thread, because a listener blocking there would prevent timeouts from firing.
- **Coalescing:** gaps detected before dispatch are summed into one callback.
- **Implementation:** a new `Server::post(name, callback)` queues one internal item, with event name `"<transport.resume>"`, on the notification worker; the worker loop and its exception handling are reused unchanged. The stopped duration is not carried in `params`: the watchdog adds it to `pendingResume_` under `resume_mtx_` and posts only when no resume item is already queued (`resumeQueued_`). The worker takes and zeroes `pendingResume_` when it runs, so gaps detected before delivery coalesce into a single callback with their sum.

### D4: Client reconciles lifecycle state on resume (client repo)

- `FireboltAccessorImpl::Connect` registers a resume listener, and `Disconnect` clears it.
- `LifecycleImpl` records the last `newState` delivered to the app from `Lifecycle2.onStateChanged`.
- On resume, the client calls `Lifecycle2.state`. If the result differs from the last delivered state and the app has an `onStateChanged` subscriber, the client delivers one synthesized `StateChange{last, current}`.
- A later real event whose `newState` equals the last delivered state is suppressed, so the app does not receive a duplicate.

- **Fan-out via the transport (decided 2026-10-21):** the synthesized change is delivered with `SubscriptionManager::dispatch("Lifecycle2.onStateChanged", payload)`, which calls the new non-pure `IHelper::dispatchLocal(owner, event, payload)`. The helper already guards its subscription map with a mutex, so the client needs no subscriber registry and no locking (client guideline §8.1). Each app callback is wrapped at subscribe time with its own last-delivered state, used for duplicate suppression. Wrappers, events, and the resume callback all run on the transport notification worker, so that state is touched by one thread only. Rejected alternative: a client-side registry guarded by a `std::mutex` in `LifecycleImpl`.

This is **phase 2**, and it is **required**: App Gateway drops lifecycle events during passivation, so without D4 the app never learns about transitions that happened while it was stopped. Duplicate suppression is kept as a defensive measure in case App Gateway behavior changes. Because `Lifecycle2.state` is sent right after a thaw, it relies on the D6 grace period.

### D6: Post-resume grace period

App Gateway may keep holding traffic for a nondeterministic time after the process thaws, so the first `waitTime_ms` of active time may not be enough.

- On resume detection, `ActiveClock` records `graceUntil = active + resumeGrace_ms`. The default for `Config::resumeGrace_ms` is 5000; 0 disables the grace period.
- **Requests:** a request pending at resume, or sent while `active < graceUntil`, is failed only when `active > max(enqueue + waitTime_ms, graceUntil)`.
- **Acknowledgement waits:** `subscribe` and `unsubscribe` called while `active < graceUntil` wait until `max(50 ms, graceUntil − active)` of active time has elapsed. A late acknowledgement within that window succeeds. Outside the window, the existing 50 ms behavior is unchanged.
- **Determinism:** the grace period is measured in active time from an injected clock, so tests drive it exactly. The fake gateway's "held after thaw" delay is scripted per test and does not depend on wall-clock timing.

*Alternatives considered:*
- Treat a late subscription acknowledgement as success without bounding it: this hides genuine failures indefinitely.
- Retry the subscription after a timeout: this risks duplicate `listen` requests upstream and needs replay logic that is out of scope.

### D5: Freeze test harness

A test in `test/integration/` forks:

- The **parent** runs a `websocketpp` fake gateway and controls the child process with `SIGSTOP` and `SIGCONT`.
- The **child** runs the transport client and reports results through a pipe.

The scenario is:

1. The child sends a request.
2. The server holds the response and the parent sends `SIGSTOP`.
3. The parent sleeps longer than `waitTime_ms`, then sends `SIGCONT`.
4. The server replies, and the child must receive the result, not `Timedout`.

`SIGSTOP` affects process clocks in the same way as cgroup and memcr stops, so the harness reproduces the failure mode realistically without Dobby.

## Risks / Trade-offs

- **CPU starvation is treated as a stop.** A running process starved longer than the gap threshold loses that time from its timeout.
  → For an embedded UI, this is acceptable and arguably correct, because the process could not have handled the response anyway. The threshold is conservative (≥ 2 s), and the stopped duration is logged.
- **Requests that genuinely fail are reported later after a resume.** If App Gateway never answers a pre-stop request, it fails `waitTime_ms` of active time after the resume, not immediately.
  → This is intended; it is bounded and deterministic.
- **The resume listener runs on the notification worker.** A slow listener delays event delivery.
  → Document this, and keep client reconciliation to a single request.
- **ABI break.** The `IGateway` vtable and the `Config` layout change. New `Config` fields are appended at the end.
  → Bump the minor version; client and transport are released together.
- **Grace extends failure detection (D6).** After a resume, a request that genuinely gets no response fails after up to `max(waitTime_ms, resumeGrace_ms)` of active time, and a `subscribe` in the grace window can block up to `resumeGrace_ms`.
  → Both bounds are configurable; setting `resumeGrace_ms = 0` restores the strict behavior.
- **Ordering between synthesized and real lifecycle events (D4).** App Gateway drops events during passivation, so held stale events are not expected. If that assumption is wrong, duplicate suppression handles events with equal states.

## Migration Plan

1. Release transport containing D1–D3 and D5. This is safe on its own: existing clients benefit from D1/D2 without changes.
2. Release a client that depends on that transport version and contains D4.
3. Rollback: revert transport. The client must not call `setResumeListener` if it builds against an older transport; the version pin enforces this.

## Open Questions

- Which transitions should the client reconcile: all of them, or only those involving `HIBERNATED` and `SUSPENDED`?
- What default `resumeGrace_ms` matches App Gateway's real post-thaw hold time? 5000 is a placeholder until device measurements exist.
- App Gateway drops lifecycle events during passivation. Does it also drop **other** subscribed events? That would be an app-visible gap, outside this change.

Resolved (2026-10-21):
- App Gateway drops lifecycle events during passivation, so D4 is required.
- App Gateway may keep holding traffic after the thaw, for a nondeterministic time. This is handled by D6.
- The gap threshold is configurable as `Config::stopGapThreshold_ms`.

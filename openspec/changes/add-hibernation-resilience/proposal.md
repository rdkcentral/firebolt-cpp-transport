## Why

Applications using the Firebolt C++ libraries run in Dobby (OCI) containers that the platform can pause (cgroup freezer) or hibernate (memcr checkpoint/restore). Neither mechanism notifies the process, and `std::chrono::steady_clock` keeps advancing while the process is stopped. After a resume, the transport sees every in-flight request as older than `waitTime_ms` and fails it with `Error::Timedout`, even though App Gateway paused traffic and will still deliver the response. This violates the hibernation epic's AC2 (graceful API handling) and AC6 (no application-level workarounds).

## Delivery Principle: Test-First

TDD is the first-order rule for this change: every behavior below is introduced by a failing, deterministic, in-process test before any production code (design D0). Test seams come first: an injectable clock and a scripted fake gateway. The `SIGSTOP`/`SIGCONT` freeze harness confirms the result end to end; it does not drive the design. This directly serves epic AC5.

## What Changes

- Transport measures request and acknowledgement timeouts in **active time**: time during which the process was actually running. A gap much longer than the watchdog interval counts as stopped time and does not count toward any timeout.
- Transport detects a resume, meaning a stopped-time gap, and reports it through a new, lifecycle-agnostic resume callback on `IGateway`, with the stopped duration.
- The gap threshold is configurable through a new `Config::stopGapThreshold_ms`.
- **Post-resume grace:** App Gateway may keep holding traffic for a nondeterministic time after the process thaws. For `Config::resumeGrace_ms` after a resume, request deadlines and acknowledgement waits are extended so that held responses are not reported as timeouts.
- The 50 ms subscribe and unsubscribe acknowledgement waits measure active time, so a freeze during the wait does not fail the call.
- Client (`firebolt-cpp-client`, a separate repo) registers for the transport resume callback. App Gateway **drops** lifecycle events during passivation, so on resume the client calls `Lifecycle2.state` and, if the state changed, delivers one `Lifecycle2.onStateChanged` notification to existing subscribers.
- Every behavior is delivered test-first (see Delivery Principle). A freeze harness (`SIGSTOP`/`SIGCONT`) is the final end-to-end check (AC5).
- **BREAKING (ABI):** adds a virtual method to `IGateway` and fields to `Config`. Source-compatible for callers; consumers must rebuild.

## Non-goals

- AppGatewayResponder pause/resume (AC4). It is assumed to be implemented by App Gateway: the WebSocket survives the stop, and held traffic is delivered on resume.
- Automatic reconnect and subscription replay if the socket does not survive. This is a possible follow-up change.
- Detecting the stop *before* it happens or pausing outgoing traffic proactively. The platform gives no in-process signal before the stop.
- Media pause and restore (AC3).
- Validating with real memcr or Dobby on a device. The harness uses `SIGSTOP`, which affects process clocks in the same way.

## Capabilities

### New Capabilities
- `hibernation-resilience`: transport behavior across a process stop. This covers timeouts based on active time, resume detection, and the resume callback.
- `client-lifecycle-reconciliation`: client behavior on transport resume. This covers lifecycle state reconciliation without app involvement (implemented in `firebolt-cpp-client`).

### Modified Capabilities
_None._ `transport_layer_spec.md` remains valid. Timeout semantics are refined and specified in `hibernation-resilience`.

## Impact

- **transport:** `src/gateway.cpp` (`Client::checkPromises`, `Caller` timestamp, `GatewayImpl::subscribe`/`unsubscribe`, watchdog thread), a new internal `src/active_clock.h`, `include/firebolt/gateway.h` (resume callback), `include/firebolt/config.h.in` (`stopGapThreshold_ms`, `resumeGrace_ms`), unit tests in `test/unit/`, and a new freeze harness in `test/integration/`.
- **client:** `src/firebolt.cpp` (`FireboltAccessorImpl::Connect`), `src/lifecycle_impl.{h,cpp}`, `test/unit/lifecycleTest.cpp`. Requires a transport version containing this change.
- **Behavior:**
  - CPU starvation that is not a freeze but exceeds the gap threshold is also treated as stopped time, which extends the effective timeout.
  - After a resume, a request that genuinely gets no response fails after at most `max(waitTime_ms, resumeGrace_ms)` of active time.
  - See design.md.

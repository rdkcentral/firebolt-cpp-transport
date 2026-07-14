# Coding Guidelines — firebolt-cpp-transport

> **Target audience**: AI agents (Copilot, openspec), contributors, and reviewers.
> **Status**: Each rule is labeled *(enforce)*, *(adopt going forward)*, or *(anti-pattern)*.
> **Language standard**: C++17 (`CXX_STANDARD 17`, `CXX_STANDARD_REQUIRED YES` in `src/CMakeLists.txt` and `test/CMakeLists.txt`). Do not use features from C++20 or later.
> **Format enforcement**: clang-format (`.clang-format`) and the CI format-check job handle all whitespace and layout rules — those are not restated here.
> **How to verify locally**: `./fmt.sh --fix` (reformat), `./test.sh` (build + test).

---

## Contents

1. [Architecture and Module Boundaries](#1-architecture-and-module-boundaries)
2. [Naming Conventions](#2-naming-conventions)
3. [Memory Ownership and Smart Pointer Usage](#3-memory-ownership-and-smart-pointer-usage)
4. [Threading and Async Patterns](#4-threading-and-async-patterns)
5. [Error Handling](#5-error-handling)
6. [Logging](#6-logging)
7. [Testing Patterns](#7-testing-patterns)
8. [JSON Types and Serialization](#8-json-types-and-serialization)
9. [Build System and Tooling](#9-build-system-and-tooling)
10. [Assumptions and Open Questions](#10-assumptions-and-open-questions)
11. [Relationship to Other Policy Files](#11-relationship-to-other-policy-files)

---

## 1. Architecture and Module Boundaries

### Rationale
The repo is a pure outbound WebSocket + JSON-RPC transport library. The public surface is intentionally small. All SDK complexity lives outside this repo. Keeping the boundary strict prevents leaking websocketpp or implementation types into downstream SDKs.

---

### 1.1 Public API is exactly `include/firebolt/` *(enforce)*

Every header placed in `include/firebolt/` is part of the public ABI consumed by downstream Firebolt C++ SDKs. Currently: `gateway.h`, `helpers.h`, `json_types.h`, `types.h`, `logger.h`, plus generated `config.h` and `transport_export.h`.

**Rule**: Do not add websocketpp, boost, or any third-party type to any header under `include/firebolt/`. All third-party includes stay inside `src/`.

Evidence: `transport.h` (which includes `<websocketpp/...>`) lives in `src/`, not `include/`.

---

### 1.2 `IGateway` is the sole public interface for transport *(enforce)*

`IGateway` (`include/firebolt/gateway.h`) is what consumers call. The concrete `GatewayImpl` class is defined entirely inside `src/gateway.cpp` and is never named outside that file. Access is only through the factory:

```cpp
// include/firebolt/gateway.h
FIREBOLTTRANSPORT_EXPORT IGateway& GetGatewayInstance();
```

**Rule**: Do not expose `GatewayImpl`, `Transport`, `Client`, `Server`, or `IClientTransport` in any public or private header outside `src/`. If a new subsystem is needed, define a new `I<Name>` interface in `include/firebolt/` and keep the impl in `src/`.

Evidence: `GatewayImpl`, `Client`, `Server`, `IClientTransport` are all defined entirely within `src/gateway.cpp` with no forward declaration elsewhere.

---

### 1.3 `IHelper` / `HelperImpl` follows the same pattern *(enforce)*

`IHelper` is in `include/firebolt/helpers.h`. `HelperImpl` is in `src/helpers_impl.h` (private header). Access is only through:

```cpp
// include/firebolt/helpers.h
FIREBOLTTRANSPORT_EXPORT IHelper& GetHelperInstance();
```

**Rule**: `HelperImpl` must not appear in any public header or be instantiated directly outside of `src/helpers_impl.cpp` (which contains `GetHelperInstance()`). Tests that need a `HelperImpl` instance must include `src/helpers_impl.h` explicitly — this is a test-only inclusion and is acceptable.

Evidence: `test/unit/helperTest.cpp` includes `"helpers_impl.h"` for direct construction in unit tests.

---

### 1.4 `Transport` is a private implementation detail *(enforce)*

`Transport` (`src/transport.h`) manages the websocketpp connection. It is not an interface and has no virtual methods. It is never referenced from `include/`.

**Rule**: Do not make `Transport` public or add it to `include/firebolt/`. If a new transport backend is needed (e.g., TLS), do it by extending `Transport` internally or adding a new private class, not by adding a new public interface.

---

### 1.5 No server-side WebSocket sockets in production code *(enforce)*

This library is a pure client (outbound connection only). Server-side WebSocket code (`websocketpp::server<...>`) appears only in `test/unit/gatewayTest.cpp` and `test/unit/transportTest.cpp` as local mock servers.

**Rule**: Never add `websocketpp::server` or any listening socket to `src/`. If a test needs a mock server, write it in `test/unit/` using the existing pattern in `gatewayTest.cpp`.

---

### 1.6 Singletons are returned by reference, not pointer *(enforce)*

```cpp
IGateway& GetGatewayInstance();   // gateway.h
IHelper& GetHelperInstance();     // helpers.h
```

Both factory functions return `static` local instances by reference.

**Rule**: New factories must follow this signature (`T&`, not `T*`, not `std::shared_ptr<T>`). The caller must not store the reference beyond the lifetime of the library.

---

### 1.7 No new third-party dependency without a Dockerfile entry *(enforce)*

The build environment is hermetic: all library dependencies for production code are installed exclusively in `.github/Dockerfile`. A host-side CMake configure may succeed if those libraries happen to be present on the developer's machine, but the resulting binary diverges from CI and cannot be trusted.

Currently approved production libraries and their `find_package()` declarations in `src/CMakeLists.txt`:

| Library | `find_package()` call | Used in |
|---------|-----------------------|---------|
| `nlohmann_json` | `nlohmann_json CONFIG REQUIRED` | `include/firebolt/gateway.h`, `include/firebolt/helpers.h`, `include/firebolt/json_types.h`, all of `src/` |
| `websocketpp` | `websocketpp CONFIG REQUIRED` | `src/transport.h`, `src/transport.cpp`, `src/gateway.cpp` |
| Boost.System | `Boost CONFIG REQUIRED COMPONENTS system` | Asio backend required by websocketpp |

`nlohmann_json_schema_validator` is installed in `.github/Dockerfile` and declared in `test/CMakeLists.txt` (`nlohmann_json_schema_validator CONFIG REQUIRED`). It is a **test-only** dependency. It must not be added to `src/CMakeLists.txt` or included from `src/` or `include/`.

Evidence: `src/CMakeLists.txt` `find_package` lines confirmed. `test/CMakeLists.txt` `find_package(nlohmann_json_schema_validator ...)` and `target_link_libraries(${UNIT_TESTS_APP} PRIVATE ... nlohmann_json_schema_validator::validator ...)` confirmed.

**Rule**: Before adding `#include` of any new third-party library to `src/` or `include/firebolt/`, first add its build recipe to `.github/Dockerfile` and its `find_package()` call to `src/CMakeLists.txt`. A PR that adds the include without the Dockerfile entry will build on machines where the library happens to be installed locally but will fail in CI.

**Cleanup**: None — all current production includes are covered by the three approved libraries above.

---

## 2. Naming Conventions

### Rationale
These are observed from every source and header file in the repo. Consistency matters here because downstream SDKs mirror these names in their own wrappers.

---

### 2.1 Classes and structs: PascalCase *(enforce)*

| Example | File |
|---------|------|
| `IGateway`, `IHelper`, `IClientTransport` | `include/firebolt/gateway.h`, `include/firebolt/helpers.h`, `src/gateway.cpp` |
| `Transport`, `GatewayImpl`, `HelperImpl` | `src/transport.h`, `src/gateway.cpp`, `src/helpers_impl.h` |
| `Client`, `Server`, `Caller` | `src/gateway.cpp` |
| `SubscriptionData`, `SubscriptionManager` | `include/firebolt/helpers.h` |
| `ErrorInfo`, `Result` | `include/firebolt/types.h` |

**Rule**: All class and struct names use PascalCase. No `snake_case` classes.

---

### 2.2 Interfaces: `I` prefix *(enforce)*

Every abstract class with pure virtual methods uses an `I` prefix: `IGateway`, `IHelper`, `IClientTransport`, `NL_Json_Basic` (the json base — exception noted below).

**Rule**: A class with `= 0` pure virtual methods must have an `I` prefix. `NL_Json_Basic` in `include/firebolt/json_types.h` does not follow this rule and is a known inconsistency — do not replicate it in new code.

> **[MAINTAINER CONFIRMATION NEEDED — item A]** Should `NL_Json_Basic` be renamed to `INL_Json_Basic` in a future minor version, or is the current name intentional because it is not a true behavioral interface?

---

### 2.3 Methods: camelCase *(enforce)*

| Example | File |
|---------|------|
| `getNextMessageID()`, `fromJson()`, `has_value()` | `src/transport.h`, `include/firebolt/json_types.h`, `include/firebolt/types.h` |
| `stopNotificationWorker()`, `ensureNotificationWorkerStarted()` | `src/gateway.cpp` |
| `checkRequiredFields()`, `isAnySubscriber()` | `include/firebolt/json_types.h`, `src/gateway.cpp` |

**Rule**: All method names (public and private) use camelCase, including getters/setters (`value()`, `error()`, `errorInfo()`). No `get_value()` or `GetValue()` style.

---

### 2.4 Private data members: trailing underscore *(adopt going forward)*

`Transport`, `HelperImpl`, and `SubscriptionManager` all use trailing underscores consistently:

```cpp
// src/transport.h
std::unique_ptr<...> client_;
std::atomic<TransportState> connectionStatus_;
std::atomic<unsigned> id_counter_ = 0;
MessageCallback messageReceiver_;
```

```cpp
// src/helpers_impl.h
Firebolt::Transport::IGateway& gateway_;
std::mutex mutex_;
std::map<uint64_t, SubscriptionData> subscriptions_;
uint64_t currentId_{0};
```

**Inconsistency**: `GatewayImpl` and the inner `Client`/`Server` classes in `src/gateway.cpp` do NOT use trailing underscores (`connectionChangeListener`, `transport`, `client`, `server`, `queue`, `invokes`).

**Rule going forward**: New classes must use trailing underscores for all private/protected data members. When editing `GatewayImpl`/`Client`/`Server`, new members added to those classes should also use trailing underscores to converge on the consistent style.

> **[MAINTAINER CONFIRMATION NEEDED — item B]** Is a bulk rename of the existing non-underscore members in `GatewayImpl`/`Client`/`Server` in scope, or should it be deferred to avoid disrupting in-flight branches?

---

### 2.5 `constexpr` local constants: `k` prefix camelCase *(adopt going forward)*

```cpp
// src/gateway.cpp
constexpr auto kSubscribeAckTimeout = std::chrono::milliseconds(50);
constexpr auto kUnsubscribeAckTimeout = std::chrono::milliseconds(50);
```

**Inconsistency**: File-scope runtime statics use a different convention (`static unsigned runtime_waitTime_ms = 3000`), and the Logger's class static uses PascalCase (`static constexpr uint16_t MaxBufSize = 1024`).

**Rule going forward**: `constexpr` named constants (local or namespace-scope) use `k` prefix + camelCase. File-scope non-`constexpr` runtime configuration statics (like `runtime_waitTime_ms`) are an existing pattern — adopt `k` prefix when introducing new ones.

> **[MAINTAINER CONFIRMATION NEEDED — item C]** Should `runtime_waitTime_ms` and `watchdog_interval_ms` be migrated to `constexpr` (or at least renamed with `k` prefix), given they are only ever written once during `connect()`?

---

### 2.6 Type aliases: PascalCase *(enforce)*

```cpp
// include/firebolt/gateway.h
using EventCallback = std::function<void(void* usercb, const nlohmann::json& params)>;
using ConnectionChangeCallback = std::function<void(const bool connected, const Firebolt::Error error)>;

// src/gateway.cpp
using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
using MessageID = uint32_t;
```

**Rule**: All `using` type aliases use PascalCase. No `snake_case` aliases.

---

### 2.7 Macros: `FIREBOLT_` prefix, ALL_CAPS *(enforce)*

```cpp
FIREBOLT_LOG_ERROR("Gateway", "...");
FIREBOLT_LOG_WARNING("Transport", "...");
FIREBOLT_LOG_NOTICE("Gateway", "...");
FIREBOLT_LOG_INFO("Gateway", "...");
FIREBOLT_LOG_DEBUG("Transport", "...");
```

The export macro `FIREBOLTTRANSPORT_EXPORT` is generated by CMake's `GenerateExportHeader`. Do not define it manually.

**Rule**: All macros introduced in this repo use `FIREBOLT_` prefix and ALL_CAPS. No new macros without the prefix.

---

### 2.8 Namespaces: nested compound style *(enforce)*

```cpp
namespace Firebolt::Transport { ... }    // src/gateway.cpp, src/transport.h, src/utils.h
namespace Firebolt::Helpers { ... }      // include/firebolt/helpers.h, src/helpers_impl.h
namespace Firebolt::JSON { ... }         // include/firebolt/json_types.h
namespace Firebolt { ... }               // include/firebolt/types.h, include/firebolt/logger.h
```

**Rule**: Use compound `namespace A::B {}` (C++17 style). Do not nest with separate braces (`namespace A { namespace B { } }`). All new code must live under the `Firebolt` top-level namespace.

---

### 2.9 Enum classes: PascalCase values *(enforce)*

```cpp
// include/firebolt/types.h
enum class Error : int32_t { None = 0, General = 1, Timedout = 2, ... };
enum class LogLevel : uint8_t { Error, Warning, Notice, Info, Debug, MaxLevel };
```

**Rule**: Always use `enum class`, never unscoped `enum`. Enum values use PascalCase. Numeric constants for error codes are set explicitly (`= -32600`) to match the JSON-RPC 2.0 specification.

---

### 2.10 Log module strings: short noun *(enforce)*

```cpp
FIREBOLT_LOG_DEBUG("Gateway", "...");
FIREBOLT_LOG_DEBUG("Transport", "...");
FIREBOLT_LOG_ERROR("Event", "...");
FIREBOLT_LOG_ERROR("Getter", "...");
```

**Rule**: The first argument to any `FIREBOLT_LOG_*` macro is a short, constant string identifying the subsystem. Use the existing set: `"Gateway"`, `"Transport"`, `"Event"`, `"Getter"`. New subsystems should pick a new short noun. Do not use file names or function names as the module string — those are controlled separately by `Logger::setFormat`.

---

### 2.11 `auto` usage and explicit return types *(enforce)*

`auto` is used in this codebase in four narrowly-defined situations only. Use it outside these categories and the type contract becomes invisible to the reader without IDE assistance — a maintenance hazard in a multi-consumer ABI.

| Permitted use | Example from codebase | Justification |
|---------------|-----------------------|---------------|
| Lambda assigned to a variable | `auto onMessage = [&](const nlohmann::json& msg) {};` (`src/gateway.cpp`) | Lambda type is unnamed by the language |
| Iterator returned by a standard algorithm | `auto it = std::find_if(eventList.begin(), eventList.end(), pred);` (`src/gateway.cpp`) | Iterator type is implementation-defined |
| Future from `get_future()` | `auto future = c->promise.get_future();` (`src/gateway.cpp`) | Full type is `std::future<Result<nlohmann::json>>` — verbose but still knowable; acceptable because it is immediately consumed or returned |
| Chrono time point | `auto now = std::chrono::system_clock::now();` (`src/logger.cpp`), `auto t0_ct = std::chrono::steady_clock::now();` (`src/transport.cpp`) | Full type is a long template instantiation |

**Rule**: Do not use `auto` for any variable whose type is a short, named type: `Firebolt::Error`, `unsigned`, `bool`, `std::string`, `nlohmann::json`, `MessageID`, `SubscriptionId`, etc. Spell those out. A wrong inference is silent and produces a bug only at the call site.

**Rule**: Never write `auto` as the declared return type of a named function or method, and never use trailing return type syntax (`auto func() -> T`). Every function and method definition across `src/` and `include/` carries a fully spelled-out return type. This is confirmed without exception across all production files.

**Anti-pattern** — AI-generated patterns that must not appear:
```cpp
auto err = gateway_.send(method, params);           // WRONG: use Firebolt::Error err = ...
auto result = helper_.get<String, std::string>(...); // WRONG: use Result<std::string> result = ...
auto connect(const Config& cfg, ...) -> Firebolt::Error; // WRONG: use Firebolt::Error connect(...)
```

**Cleanup**: No existing violation found in `src/` or `include/`. All production functions carry explicit return types.

---

## 3. Memory Ownership and Smart Pointer Usage

### Rationale
The codebase mixes smart pointers and raw pointers deliberately. The raw pointer usage for subscription callbacks is load-bearing and cannot be naively replaced.

---

### 3.1 `std::unique_ptr` for single-owner heap objects *(enforce)*

```cpp
// src/transport.h
std::unique_ptr<websocketpp::client<websocketpp::config::asio_client>> client_;

// test/unit/gatewayTest.cpp
std::unique_ptr<std::thread> m_serverThread;
```

**Rule**: Heap objects with a single clear owner use `std::unique_ptr`. Prefer brace-initialized (`std::make_unique<T>(...)`) over `reset(new T(...))`. The single exception is `Transport::start()` which uses `connectionThread_.reset(new ...)` due to websocketpp's own `shared_ptr`-based thread type — do not replicate this pattern.

---

### 3.2 `std::shared_ptr` for multi-owner objects *(enforce)*

```cpp
// src/gateway.cpp (Client class)
std::map<MessageID, std::shared_ptr<Caller>> queue;
```

`Caller` is shared because a response may arrive after the request queue has been pruned (timeout path). The websocketpp connection thread (`connectionThread_`) is also a `websocketpp::lib::shared_ptr<websocketpp::lib::thread>` because websocketpp owns the thread type.

**Rule**: Use `std::shared_ptr` only when the lifetime genuinely requires shared ownership. If you think you need `shared_ptr` for a new type, first confirm it cannot use `unique_ptr` or a reference.

---

### 3.3 `void*` for subscription callback user-data *(enforce — do not replace)*

```cpp
// include/firebolt/gateway.h
using EventCallback = std::function<void(void* usercb, const nlohmann::json& params)>;
virtual Firebolt::Error subscribe(const std::string& event, EventCallback callback, void* usercb) = 0;

// src/helpers_impl.h
void* notificationPtr = reinterpret_cast<void*>(&subscriptions_[newId]);
```

`void*` here is intentional: `HelperImpl` stores `SubscriptionData` objects in a `std::map<uint64_t, SubscriptionData>`. The address of each map entry (stable after insertion) is passed as `void*` to the gateway layer. The typed callback `onPropertyChangedCallback<JsonType, ...>` in `helpers.h` casts back via `reinterpret_cast<SubscriptionData*>`.

**Rule**: Do not attempt to replace `void* usercb` in `EventCallback` with a typed pointer or `std::any` — it is the public API type and downstream SDKs depend on it. When adding new subscription types, follow the same `SubscriptionData` + `void*` indirection pattern already in `HelperImpl`.

> **[MAINTAINER CONFIRMATION NEEDED — item D]** The `subscriptions_` map in `HelperImpl` is erased during `unsubscribeAll()` while the address of the erased entry may still be held as `void*` in the Gateway's event list. Is there a guaranteed teardown order that prevents use-after-free here (e.g., `unsubscribeAll()` always calls `gateway_.unsubscribe()` before erasing from the map)?

---

### 3.4 References for constructor-injected dependencies *(enforce)*

```cpp
// src/gateway.cpp (Client)
IClientTransport& transport_;

// src/helpers_impl.h (HelperImpl)
Firebolt::Transport::IGateway& gateway_;

// include/firebolt/helpers.h (SubscriptionManager)
IHelper& helper_;
void* owner_;
```

**Rule**: Injected dependencies that outlive the dependent object are stored as references (`T&`), not pointers. This makes lifetime ownership unambiguous. The constructor must not accept a `nullptr` dependency — use `assert(ptr != nullptr)` if the source must be a pointer before binding to a reference.

---

### 3.5 No raw `new`/`delete` in new code *(enforce)*

Production code contains no direct `new`/`delete` calls. All heap allocation uses smart pointers or STL containers.

**Rule**: Never introduce raw `new`/`delete` in `src/` or `include/`. In tests, use `std::make_unique` even for short-lived objects.

---

## 4. Threading and Async Patterns

### Rationale
The transport has three concurrent threads in steady state: the websocketpp I/O thread, a message-dispatch worker thread, and a notification worker thread. A watchdog thread also runs. All inter-thread communication uses queues with condition variables — never direct cross-thread calls.

---

### 4.1 Worker threads use atomic stop-flag + condition variable *(enforce)*

Both `Transport::stopMessageWorker()` and `Server::stopNotificationWorker()` follow the same idiom:

```cpp
// src/transport.cpp
void Transport::stopMessageWorker()
{
    stopMessageWorker_ = true;
    messageQueueCv_.notify_all();
    if (messageWorkerThread_.joinable())
    {
        messageWorkerThread_.join();
    }
}
```

The worker loop waits with a predicate that checks both the stop flag and the queue:

```cpp
messageQueueCv_.wait(lock, [this] { return stopMessageWorker_ || !messageQueue_.empty(); });
```

**Rule**: Any new worker thread introduced in this codebase must use `std::atomic<bool>` for its stop flag (not a regular bool), signal via `notify_all()`, and call `.join()` before the owning object is destroyed. Never use `std::this_thread::sleep_for` in a worker loop — use condition variable wait with a predicate instead.

---

### 4.2 Websocketpp callbacks dispatch to a queue; never execute directly *(enforce)*

```cpp
// src/transport.cpp — Transport::onMessage()
{
    std::lock_guard<std::mutex> lock(messageQueueMutex_);
    messageQueue_.push(msg->get_payload());
}
messageQueueCv_.notify_one();
```

Callbacks from websocketpp run on the websocketpp I/O thread. They are never processed inline — always enqueued and handled by the `processQueuedMessages()` worker.

**Rule**: Do not call `messageReceiver_` or `connectionReceiver_` directly from a websocketpp handler (onMessage, onOpen, onClose, onFail). Any new message type must go through the queue. Violating this creates a deadlock risk when the receiver tries to acquire a mutex held by the I/O thread.

---

### 4.3 `std::lock_guard` for simple mutex sections, `std::unique_lock` for condition variable wait *(enforce)*

```cpp
// src/gateway.cpp
std::lock_guard lck(eventMap_mtx);                     // simple section, CTAD OK
std::lock_guard<std::mutex> lock(notificationQueueMutex_);  // simple section, explicit type OK
std::unique_lock<std::mutex> lock(notificationQueueMutex_); // only when needed for cv.wait()
```

Both CTAD (`lock_guard lck(mtx)`) and explicit (`lock_guard<std::mutex> lock(mtx)`) are used. Either is acceptable.

**Rule**: Use `std::lock_guard` for all simple locked sections. Use `std::unique_lock` only when the lock needs to be passed to a condition variable `.wait()`. Never use `std::unique_lock` where `std::lock_guard` suffices.

---

### 4.4 `std::future` / `std::promise` for request/response correlation *(enforce)*

```cpp
// src/gateway.cpp (Client::request)
std::shared_ptr<Caller> c = std::make_shared<Caller>(id);
auto future = c->promise.get_future();
// ... send
return future;

// Client::response
c->promise.set_value(Result<nlohmann::json>{message["result"]});
```

**Rule**: Request/response correlation is done exclusively with `std::future`/`std::promise`. Do not introduce callback-based response handling for new request types — that would require converting the existing `IGateway::request()` return type, which is part of the public ABI.

---

### 4.5 Watchdog is a polling thread; keep its interval configurable *(enforce)*

```cpp
// src/gateway.cpp
static unsigned runtime_waitTime_ms = 3000;
static unsigned watchdog_interval_ms = 500;

watchdogThread = std::thread([this]() {
    while (watchdogRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(watchdog_interval_ms));
        client.checkPromises();
    }
});
```

The watchdog uses `sleep_for` (acceptable here — it is polling by design, not a worker thread). Both timeout values are derived from `Firebolt::Config` at connect time.

**Rule**: Do not hardcode timeout or interval values in new code. They must come from `Firebolt::Config` and be stored in the file-scope statics (or a new config struct) during `connect()`.

---

## 5. Error Handling

### Rationale
This library is a transport layer used by production embedded SDKs. Exceptions cannot propagate across the public ABI. All error paths must be observable by the caller without crashing or throwing.

---

### 5.1 Public APIs return `Firebolt::Error` or `Result<T>` — never throw *(enforce)*

```cpp
// include/firebolt/gateway.h
virtual Firebolt::Error connect(...) = 0;
virtual std::future<Firebolt::Result<nlohmann::json>> request(...) = 0;

// include/firebolt/helpers.h
virtual Result<void> set(const std::string& methodName, const nlohmann::json& parameters) = 0;
template <typename JsonType, typename PropertyType>
Result<PropertyType> get(const std::string& methodName, ...);
```

**Rule**: Every method on a public interface (anything in `include/firebolt/`) must return `Firebolt::Error` or `Result<T>`. Never throw from a public API. Never return a raw bool for success/failure where the error value matters.

---

### 5.2 `Result<T>` usage contract *(enforce)*

```cpp
// Check: if (result)
// Dereference: *result  or  result->field
// Error: result.error()
// Error info: result.errorInfo()
```

`Result<T>` has `operator bool()` that returns `true` iff `has_value()` is true (i.e., `Error::None`). The `value()` / `operator*` / `operator->` accessors are only valid after the bool check.

**Rule**: Always gate dereference with the bool check. Never call `*result` or `result->` without first checking `if (result)`. For `Result<void>`, `operator bool()` checks `error_ == Error::None` — use it the same way.

---

### 5.3 JSON parse errors: catch, log, return `Error::InvalidParams` *(enforce)*

```cpp
// include/firebolt/helpers.h — IHelper::get<>()
catch (const std::exception& e)
{
    FIREBOLT_LOG_ERROR("Getter", "Cannot parse data for a getter %s, payload: %s", ...);
    return Result<PropertyType>{Firebolt::Error::InvalidParams,
                                Firebolt::ErrorInfo(static_cast<int32_t>(Firebolt::Error::Unspecified),
                                                    "Cannot parse response data")};
}
```

```cpp
// src/transport.cpp — processQueuedMessages()
catch (const std::exception&)
{
    FIREBOLT_LOG_ERROR("Transport", "Cannot parse payload: '%s'", payload.c_str());
}
```

**Rule**: Always catch JSON parse exceptions at the earliest boundary where the error can be mapped to a `Firebolt::Error`. Use `catch (const std::exception& e)`. In the transport layer (where there is no `Result<>` return), log and discard. In the helper/gateway layer, map to `Error::InvalidParams`.

---

### 5.4 `assert()` for debug-only precondition checks *(enforce)*

```cpp
// src/gateway.cpp
assert(onConnectionChange != nullptr);
assert(onMessage != nullptr);
```

**Rule**: Use `assert()` only to document invariants that must hold at development time and that cannot fail in a correctly-integrated production deployment. Do not use `assert()` for runtime input validation (e.g., a URL that could be empty). For user-facing precondition failures, return `Firebolt::Error::InvalidParams` instead.

---

### 5.5 Error propagation from websocketpp: use `mapError()` *(enforce)*

```cpp
// src/transport.cpp
static Firebolt::Error mapError(websocketpp::lib::error_code error)
{
    using EV = websocketpp::error::value;
    switch (error.value())
    {
    case EV::open_handshake_timeout:
    case EV::close_handshake_timeout:
        return Firebolt::Error::Timedout;
    // ...
    default:
        return Firebolt::Error::General;
    }
}
```

**Rule**: Never expose `websocketpp::lib::error_code` outside `src/transport.cpp`. All websocketpp errors must be translated through `mapError()` before being stored or returned. If a new websocketpp error needs distinct handling, add it to `mapError()`'s switch statement.

---

## 6. Logging

### Rationale
The logger writes to stderr (or syslog if `ENABLE_SYSLOG` is set). Log format is configurable at runtime via `Logger::setFormat()`. Log level is set at connect time via `Config::log.level`. All log calls in production code use the macros — this is consistent across the entire codebase.

---

### 6.1 Always use `FIREBOLT_LOG_*` macros *(enforce)*

```cpp
FIREBOLT_LOG_ERROR("Gateway", "Invalid payload received: %s", message.dump().c_str());
FIREBOLT_LOG_WARNING("Gateway", "No subscribers for event: %s", method.c_str());
FIREBOLT_LOG_NOTICE("Transport", "Version: %s", Version::String);
FIREBOLT_LOG_INFO("Gateway", "[disconnect] transport.disconnect() done in %ld ms, status=%d", ...);
FIREBOLT_LOG_DEBUG("Transport", "Received: %s", jsonMsg.dump().c_str());
```

**Rule**: Never use `std::cout`, `printf`, `fprintf`, or any direct output call in `src/` or `include/`. All diagnostic output goes through the `FIREBOLT_LOG_*` macros. These macros capture `__FILE__`, `__func__`, and `__LINE__` automatically.

---

### 6.2 Log level semantics *(enforce)*

| Level | When to use | Examples from codebase |
|-------|-------------|------------------------|
| `Error` | Unexpected state or failure that will affect functionality | Parse failure on incoming payload; failure to send message; invalid JSON-RPC payload |
| `Warning` | Transient issue the caller may recover from; ignorable in normal operation | Request timed out in watchdog; notification received while stopping; no subscriber for event |
| `Notice` | Important lifecycle event worth always seeing | Transport version; connect with URL; legacy RPCv1 mode |
| `Info` | Timing data and lifecycle step completions | Thread join timing; disconnect step timing |
| `Debug` | Internal state transitions useful only for debugging | Message send/receive content; subscribe/unsubscribe ACK timing; worker thread start/stop |

**Rule**: Do not use `Error` level for expected recoverable paths (e.g., subscribe ACK timeout during shutdown). Do not use `Debug` level for data that appears in every request path at production volumes — it would flood the log.

---

### 6.3 Printf-style format strings; never concatenate strings in log calls *(enforce)*

```cpp
// Correct
FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] ACK for '%s' timed out after %lld ms", event.c_str(), ms);

// Wrong — do not do this
FIREBOLT_LOG_DEBUG("Gateway", std::string("[subscribe] ACK for '") + event + "' timed out");
```

**Rule**: The format argument to `FIREBOLT_LOG_*` must be a string literal. Never pass a `std::string` or a concatenated string as the format argument. This is enforced by `__attribute__((format(printf, 6, 7)))` on `Logger::log()` — the compiler will warn on format/argument mismatches.

---

### 6.4 Never log header values at non-Debug levels *(enforce)*

```cpp
// src/transport.cpp — onOpen
const auto& headers = con->get_response().get_headers();
for (const auto& header : headers)
{
    responseHeaders_[header.first] = header.second;
}
```

Response headers are stored silently. They are only readable by callers via `getResponseHeader()`.

**Rule**: If response headers ever need to be logged for debugging, log only the header name, not the value, at Info level. Never log header values at Notice or above — headers may contain authentication tokens. (Currently no header logging exists in production code — this rule prevents a future AI-introduced regression.)

---

## 7. Testing Patterns

### Rationale
The test suite mixes pure unit tests (mocked interfaces) and integration tests (live local websocketpp server). The integration tests are the primary coverage path for `Transport` and `GatewayImpl`. Coverage is measured with gcovr and uploaded as CI artifacts.

---

### 7.1 New test files: `test/unit/*Test.cpp` *(enforce)*

All unit test files follow the `*Test.cpp` naming pattern:
`gatewayTest.cpp`, `helperTest.cpp`, `transportTest.cpp`, `loggerTest.cpp`, `json_typesTest.cpp`.

The CMakeLists.txt glob is `file(GLOB UNIT_TESTS CONFIGURE_DEPENDS unit/*Test.cpp)`.

**Rule**: New test files must be named `<Component>Test.cpp` and placed in `test/unit/`. The CMake glob picks them up automatically. Do not add test files directly to `test/` root.

---

### 7.2 Test fixture names: `<Component>UTest` *(enforce)*

```cpp
class GatewayUTest : public ::testing::Test { ... };
class TransportUTest : public ::testing::Test { ... };
class HelperUTest : public ::testing::Test { ... };
class LoggerFormatUTest : public ::testing::Test { ... };
```

**Rule**: Test fixture class names use `<Component>UTest` suffix. Free-function tests (no fixture) use `TEST(<SuiteUTest>, <TestName>)` with the same naming pattern.

---

### 7.3 Unit tests mock interfaces with GMock *(enforce)*

```cpp
// test/unit/helperTest.cpp
class MockGateway : public Transport::IGateway
{
public:
    MOCK_METHOD(Error, connect, (const Config&, Firebolt::Transport::ConnectionChangeCallback), (override));
    MOCK_METHOD(std::future<Result<nlohmann::json>>, request, (const std::string&, const nlohmann::json&), (override));
    // ...
};
```

**Rule**: Unit tests that exercise logic above `Transport` (i.e., `HelperImpl` or subscription management) must mock `IGateway` using `MOCK_METHOD`. Do not spin up a real WebSocket server in a unit test — that is an integration test.

---

### 7.4 Integration tests spin up a local websocketpp server *(enforce)*

```cpp
// test/unit/gatewayTest.cpp
class GatewayUTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    const std::string m_uri = "ws://localhost:9003";
    // ...
    void TearDown() override
    {
        GetGatewayInstance().disconnect();
        if (m_serverStarted) { m_server.stop_listening(); m_server.stop(); ... }
    }
};
```

Ports currently in use: `9002` (TransportIntegrationUTest), `9003` (GatewayUTest).

**Rule**: Each integration test fixture must use a distinct port to avoid `EADDRINUSE` failures. Call `GetGatewayInstance().disconnect()` in `TearDown()` before stopping the server. Always check `m_serverThread->joinable()` before calling `join()`.

---

### 7.5 Async synchronization uses `std::promise` with a timeout, never bare sleep *(enforce)*

```cpp
// test/unit/gatewayTest.cpp
auto status = connectionFuture.wait_for(std::chrono::seconds(2));
ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
```

**Rule**: Never use `std::this_thread::sleep_for` in tests to wait for asynchronous events. Always use a `std::promise`/`std::future` pair with `wait_for()` and an `ASSERT_EQ(status, std::future_status::ready)` guard. If the timeout fires, the test must fail with a clear message.

---

### 7.6 Logger tests capture stderr with `pipe()`/`dup2()` *(enforce)*

```cpp
// test/unit/loggerTest.cpp
int pipefd[2] = {-1, -1};
pipe(pipefd);
int savedStderr = dup(STDERR_FILENO);
dup2(pipefd[1], STDERR_FILENO);
// ... call Logger
fflush(stderr);
dup2(savedStderr, STDERR_FILENO);
```

**Rule**: Do not add a configurable output stream to `Logger` just to make testing easier — that would change the public API. Use the `pipe()`/`dup2()` capture pattern for any new Logger format tests.

---

### 7.7 Comment header block above each test *(adopt going forward)*

The more recent logger tests include a structured comment block:

```cpp
// ---------------------------------------------------------------------------
// Test name: LoggerFormatUTest.LocationTrue_FunctionTrue
// Covers: logger.cpp format branch (addLocation=true, addFunction=true)
// Scenario type: success
// ---------------------------------------------------------------------------
```

**Rule going forward**: New tests in `test/unit/` should include the `// ---` comment block above each `TEST_F` or `TEST`, documenting the test name, the exact code path covered, and the scenario type (success / failure / edge case). This aids coverage analysis and is the pattern used in `loggerTest.cpp`.

---

## 8. JSON Types and Serialization

### Rationale
`nlohmann::json` is the only JSON library used and is non-negotiable (it is a fixed dependency in `.github/Dockerfile`). The `Firebolt::JSON` namespace provides a thin wrapper layer that maps JSON values to C++ types via a uniform `fromJson()` / `value()` interface. Downstream SDKs depend on this interface shape.

---

### 8.1 All JSON serialization uses `nlohmann::json` *(enforce)*

```cpp
// include/firebolt/gateway.h
#include <nlohmann/json.hpp>
using EventCallback = std::function<void(void* usercb, const nlohmann::json& params)>;
```

**Rule**: Do not introduce any other JSON library. Do not use `nlohmann::json` via the `nlohmann_json_schema_validator` path for anything other than schema validation in tests (it is a test-only dependency per `test/CMakeLists.txt`).

---

### 8.2 JSON type wrappers: `fromJson()` + `value()` interface *(enforce)*

```cpp
// include/firebolt/json_types.h
template <typename T> class NL_Json_Basic {
public:
    virtual void fromJson(const nlohmann::json& json) = 0;
    T virtual value() const = 0;
};

using String = BasicType<std::string>;
using Boolean = BasicType<bool>;
using Float = BasicType<float>;
using Unsigned = BasicType<uint32_t>;
using Integer = BasicType<int32_t>;
```

**Rule**: New JSON-mapped types in `Firebolt::JSON` must inherit from `NL_Json_Basic<T>` and implement `fromJson()` and `value()`. Do not create ad-hoc structs that manually parse `nlohmann::json` without going through this interface — consistency here is required for the `IHelper::get<JsonType, PropertyType>()` template to work correctly.

---

### 8.3 Let `nlohmann::json::get<T>()` throw on type mismatch *(enforce)*

```cpp
// include/firebolt/json_types.h — BasicType
void fromJson(const nlohmann::json& json) override { value_ = json.get<T>(); }

// test/unit/json_typesTest.cpp
EXPECT_THROW(str.fromJson(jsonInt), nlohmann::json::type_error);
```

**Rule**: Do not add `try/catch` inside `fromJson()` implementations. Let `nlohmann::json::type_error` propagate to the caller (`IHelper::get<>()` or `onPropertyChangedCallback<>()`), which has a uniform catch block that logs the error and returns `Error::InvalidParams`.

---

### 8.4 Use `.at()` not `[]` for mandatory field access *(adopt going forward)*

```cpp
// test/unit/helperTest.cpp — TestJson
void fromJson(const nlohmann::json& json) { v = json.at("value").get<int>(); }
```

`.at()` throws `nlohmann::json::out_of_range` on missing key. `[]` silently inserts a null value.

**Rule going forward**: When implementing `fromJson()` for a struct that requires a field to be present, use `.at("field")`, not `["field"]`. Optional fields may use `json.contains("field")` + `json["field"]`.

---

### 8.5 `// clang-format off` is permitted for JSON-keyed initializers *(enforce)*

```cpp
// src/logger.cpp
// clang-format off
std::map<Firebolt::LogLevel, const char*> _logLevelNames = {
    {LogLevel::Error, "Error"},
    {LogLevel::Warning, "Warning"},
    ...
};
// clang-format on
```

**Rule**: `// clang-format off` / `// clang-format on` is acceptable only to preserve alignment in dense map/struct initializers where auto-formatting would significantly reduce readability. Do not use it to bypass formatting for general code.

---

## 9. Build System and Tooling

### Rationale
The build environment is hermetic: all library dependencies live inside the Docker image built from `.github/Dockerfile`. The wrapper scripts in the repo root abstract Docker invocation, image-build-on-demand, and environment setup. Bypassing them produces a binary that diverges from CI and cannot be trusted for regression validation.

---

### 9.1 Use wrapper scripts only; never invoke cmake directly on the host *(enforce)*

| Script | Purpose |
|--------|---------|
| `./test.sh` | Build all targets and run the full unit test suite. Run before every commit. |
| `./fmt.sh` | clang-format check (dry-run, `--Werror`). Mirrors the CI format gate exactly. |
| `./fmt.sh --fix` | Reformat all `src/` and `include/` sources in-place. |
| `./build.sh +tests` | Manual CMake configure and build with tests enabled. Requires the `SYSROOT_PATH` environment variable. |

`./test.sh` and `./fmt.sh` build the Docker image (`firebolt-cpp-transport-ci:local`) automatically on first run from `.github/Dockerfile`; subsequent invocations reuse the cached image.

**Rule**: Never run `cmake`, `make`, or any equivalent tool directly on the host machine. All production dependencies (`nlohmann_json`, `websocketpp`, `GTest`, `gcovr`) are installed only inside the Docker image. A host-side configure may silently succeed if those libraries are incidentally present, producing a binary that passes locally but fails or behaves differently in CI.

Evidence: `.github/Dockerfile` installs all deps into the image only. `README.md` documents no host-side install path for the development cycle.

**Cleanup**: If `build-dev/` was created by a host-side cmake, delete it with `sudo rm -rf build-dev/` and regenerate via `./test.sh`. Do not commit any file under `build-dev/` — it is covered by `.gitignore`.

---

### 9.2 Build directory ownership and cache validity *(enforce)*

`build-dev/` is created inside the Docker container and is owned by `root` from the host filesystem's perspective.

**Rule**: If any host-side operation fails with a permission error against `build-dev/`, run `sudo rm -rf build-dev/` and re-run `./test.sh`. Do not attempt to `chown` the directory — the next Docker invocation recreates it correctly.

`build-dev/CMakeCache.txt` bakes the Docker-internal source path (`/workspace`) into the cache at configure time. If the cache was created in a different build context (e.g., the directory was moved or the image rebuilt), `test.sh` detects the stale path and wipes the cache automatically before reconfiguring. This wipe is intentional, not a bug.

**Rule**: Do not copy a `CMakeCache.txt` between machines or Docker contexts. The baked-in source path will be wrong and will cause a misconfigured build that appears to succeed.

---

## 10. Assumptions and Open Questions

### 10.1 Explicit assumptions made during analysis

These conventions are observed consistently but are not documented in any existing file. They are treated as binding rules in this document. If a maintainer disagrees, the relevant rule should be revised.

| # | Assumption | Evidence |
|---|-----------|----------|
| A1 | `GatewayImpl` is the only concrete implementation of `IGateway` in this repo. A second implementation (e.g., TLS) is not expected. | No `#ifdef` or factory-dispatch logic in `GetGatewayInstance()`. |
| A2 | The `void* usercb` / `void* notificationPtr` pattern for subscription callbacks is intentional ABI design, not a historical accident. | The address-of-map-entry pattern in `HelperImpl::subscribe()` and the typed cast in `onPropertyChangedCallback<>()` form a consistent pair. |
| A3 | `legacyRPCv1` mode is a maintenance concern only. New features must target RPC v2 (default). | The `ENABLE_LEGACY_RPC_V1=ON` CMake option is an explicit opt-in; its `false` default and the `RPCv2=true` query parameter in `buildGatewayUrl()` confirm v2 is the primary mode. |
| A4 | `Config` is a value type passed by value to `connect()`. No part of the library retains a reference to the caller's `Config`. | `connect()` signature is `connect(const Firebolt::Config& cfg, ...)` and all relevant fields are copied to local statics or members. |
| A5 | The two test ports (9002, 9003) are assumed unused on the CI machine during tests. No dynamic port allocation is used. | Hardcoded in `transportTest.cpp` and `gatewayTest.cpp`. |

---

### 10.2 Flagged items requiring maintainer confirmation before treating as binding

| Tag | Rule reference | Question |
|-----|----------------|----------|
| **[A]** | §2.2 | Should `NL_Json_Basic` be renamed `INL_Json_Basic` to follow the interface prefix convention? |
| **[B]** | §2.4 | Is a bulk rename of non-underscore private members in `GatewayImpl`/`Client`/`Server` in scope for a near-term PR? |
| **[C]** | §2.5 | Should `runtime_waitTime_ms` and `watchdog_interval_ms` be converted to `constexpr` constants (or at least renamed to `k*`)? Currently they are written during `connect()` from `Config`, so they are not compile-time constants. Clarify intended mutability. |
| **[D]** | §3.3 | Is the teardown order in `HelperImpl::~HelperImpl()` and `HelperImpl::unsubscribeAll()` safe? The map entry address (`notificationPtr`) is passed to `gateway_.unsubscribe()` before the map entry is erased — confirm this is always the case and no code path erases the map before calling the gateway. |

---

*Last updated: 2026-07-14. Derived from direct analysis of: `include/firebolt/gateway.h`, `include/firebolt/helpers.h`, `include/firebolt/json_types.h`, `include/firebolt/types.h`, `include/firebolt/logger.h`, `include/firebolt/config.h.in`, `src/gateway.cpp`, `src/transport.h`, `src/transport.cpp`, `src/helpers_impl.h`, `src/helpers_impl.cpp`, `src/utils.h`, `src/utils.cpp`, `src/logger.cpp`, `test/unit/gatewayTest.cpp`, `test/unit/helperTest.cpp`, `test/unit/transportTest.cpp`, `test/unit/loggerTest.cpp`, `test/unit/json_typesTest.cpp`, `CMakeLists.txt`, `src/CMakeLists.txt`, `test/CMakeLists.txt`, `.clang-format`, `.github/Dockerfile`.*

---

## 11. Relationship to Other Policy Files

This document is the **single authoritative source** for all coding conventions, architecture rules, testing patterns, and build tooling guidance in `firebolt-cpp-transport`. CI branch targets and release tag format are intentionally absent — they are not coding conventions.

| File | Scope |
|------|-------|
| `.github/instructions/coding-guidelines.instructions.md` *(this file)* | Coding conventions, architecture, testing patterns, build tooling |
| `CONTRIBUTING.md` | Contribution process (CLA, PR workflow) |
| `.clang-format` | Whitespace, indentation, brace style — enforced by CI; rules not restated here |
| `.github/Dockerfile` | Authoritative list of approved build and test dependencies |

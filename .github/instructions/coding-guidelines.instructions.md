---
applyTo:
  - "**/*.h"
  - "**/*.h.in"
  - "**/*.cpp"
  - "**/CMakeLists.txt"
---

# firebolt-cpp-transport — Coding Guidelines

**Scope:** This document governs code generation and modification for the `firebolt-cpp-transport` repository.
It is intended for use by both AI agents (Copilot, openspec) and human developers.

**How to read this document:**
- **Current practice** *(enforce)* — observed consistently across `src/`, `include/firebolt/`, and `test/unit/`; must be preserved in all new and modified code.
- **Recommended going forward** *(adopt going forward)* — not yet uniformly applied across all existing code (e.g., trailing-underscore members in `src/gateway.cpp`, `std::promise`/`std::future` in tests), but required for all new or modified code.
- **Anti-pattern** *(anti-pattern)* — either already present in the repo as a legacy inconsistency, or a pattern likely to be introduced by AI code generation; each entry in §12 explains why it is wrong in this specific codebase.

**Format:** All whitespace, indentation, and brace-style rules are enforced by clang-format (`.clang-format`) and the CI format-check job — those rules are not restated here.
Run `./fmt.sh --fix` to reformat `src/` and `include/` in-place. Run `./test.sh` to build all targets and run the full unit test suite inside the hermetic Docker environment.

Markers:
- `[ASSUMPTION]` — inferred from code patterns where no explicit policy document exists; full list in §13.1.
- `[MAINTAINER CONFIRMATION NEEDED]` — rule or fact that requires explicit maintainer decision before treating as binding; full list in §13.2.

---


## Contents

1. [Architecture and Module Boundaries](#1-architecture-and-module-boundaries)
2. [Naming Conventions](#2-naming-conventions)
3. [Header Guards and Include Style](#3-header-guards-and-include-style)
4. [Class Structure and Copy/Move Semantics](#4-class-structure-and-copymove-semantics)
5. [Memory Ownership and Smart Pointer Usage](#5-memory-ownership-and-smart-pointer-usage)
6. [Threading and Async Patterns](#6-threading-and-async-patterns)
7. [Error Handling](#7-error-handling)
8. [Logging](#8-logging)
9. [Testing Patterns](#9-testing-patterns)
10. [JSON Types and Serialization](#10-json-types-and-serialization)
11. [Build System and Tooling](#11-build-system-and-tooling)
12. [Anti-Patterns Catalogue](#12-anti-patterns-catalogue)
13. [Assumptions and Open Questions](#13-assumptions-and-open-questions)
14. [Relationship to Other Policy Files](#14-relationship-to-other-policy-files)

---

## 1. Architecture and Module Boundaries

### Rationale
The repo is a pure outbound WebSocket + JSON-RPC transport library. The public surface is intentionally small. All SDK complexity lives outside this repo. Keeping the boundary strict prevents leaking websocketpp or implementation types into downstream SDKs.

---

### 1.1 Public API is exactly `include/firebolt/` *(enforce)*

Every header placed in `include/firebolt/` is part of the public ABI consumed by downstream Firebolt C++ SDKs. Source headers: `gateway.h`, `helpers.h`, `json_types.h`, `types.h`, `logger.h`, `config.h.in`, `version.h.in`. CMake additionally generates and installs `config.h` (from `config.h.in`), `transport_version.h` (from `version.h.in`), and `transport_export.h` (from `GenerateExportHeader`) under the `firebolt/` include prefix at build time.

**Rule**: Do not add `<websocketpp/...>` or `<boost/...>` includes, or any type from those libraries, to any header under `include/firebolt/`. The sole approved third-party type in public headers is `nlohmann::json`, which is already part of the established public ABI: `EventCallback`, `IGateway::request()`, `IGateway::send()`, and all JSON type adapters in `json_types.h` depend on `nlohmann::json` as the wire-format representation. Do not add any further third-party includes to `include/firebolt/` beyond `nlohmann/json.hpp`.

Evidence: `transport.h` (which includes `<websocketpp/...>`) lives in `src/`, not `include/`. `include/firebolt/gateway.h` and `include/firebolt/helpers.h` include `<nlohmann/json.hpp>` — this is intentional and must remain.

---

### 1.2 `IGateway` is the sole public interface for transport *(enforce)*

`IGateway` (`include/firebolt/gateway.h`) is what consumers call. The concrete `GatewayImpl` class is defined entirely inside `src/gateway.cpp` and is not declared in any header — it is unreachable by name through any `#include` path. Access is only through the factory:

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

`Transport` (`src/transport.h`) manages the websocketpp connection. It is not an interface and has only a virtual destructor — no other virtual methods. It is never referenced from `include/`.

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
| `getNextMessageID()`, `fromJson()`, `errorInfo()` | `src/transport.h`, `include/firebolt/json_types.h`, `include/firebolt/types.h` |
| `stopNotificationWorker()`, `ensureNotificationWorkerStarted()` | `src/gateway.cpp` |
| `checkRequiredFields()`, `isAnySubscriber()` | `include/firebolt/json_types.h`, `src/gateway.cpp` |

**Rule**: All new method names (public and private) use camelCase, including getters/setters (`value()`, `error()`, `errorInfo()`). No `get_value()` or `GetValue()` style.

**Exception**: `Result<T>::has_value()` uses underscores deliberately — it mirrors the `std::optional<T>` interface. It is not a camelCase method and must not be cited as one. Do not introduce new underscore-separated method names modeled on this exception.

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

**Inconsistency**: Trailing-underscore usage in `src/gateway.cpp` is mixed across the three internal classes:
- `GatewayImpl` data members have no trailing underscores: `connectionChangeListener`, `transport`, `client`, `server`, `watchdogThread`, `watchdogRunning`, `legacyRPCv1`.
- `Client` has `transport_` (with underscore) but `queue`, `queue_mtx`, `invokes`, `invokes_mtx` (without).
- `Server` has `notificationQueue_`, `notificationQueueMutex_`, `notificationQueueCv_`, `notificationWorkerThread_`, `stopNotificationWorker_` (with underscores) but `eventList`, `eventMap_mtx` (without).

These classes were developed incrementally; the newer notification-related members in `Client` and `Server` adopted the trailing-underscore style while older event-tracking members did not.

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

## 3. Header Guards and Include Style

### 3.1 Header guards: `#pragma once` *(enforce)*

**Current practice (confirmed in every header):**
- All public headers under `include/firebolt/` use `#pragma once`: `gateway.h`, `helpers.h`, `json_types.h`, `types.h`, `logger.h`, `config.h.in` — confirmed.
- All private `src/` headers that are true multi-include guards use `#pragma once`: `src/transport.h`, `src/utils.h` — confirmed.

**Exception**: `src/helpers_impl.h` has no header guard and no `#pragma once`. This is intentional — it is an implementation file in `.h` form, containing the full `HelperImpl` class definition inline. It is included in exactly two translation units: `src/helpers_impl.cpp` (production singleton) and `test/unit/helperTest.cpp` (test-only direct instantiation). All methods are implicitly `inline`, making multiple-TU inclusion ODR-safe. (Adding `#pragma once` would also be harmless — it only prevents double-inclusion within a single TU, which is already not a concern here. The file simply has no guard because it is a deliberately narrow-use implementation header.)

**Rule**: All new `.h` files under `include/firebolt/` and `src/` must begin with `#pragma once`. Do not add `#ifndef`/`#define`/`#endif` include guards to hand-written headers — generated headers may use whatever guard style their generator emits (e.g., `transport_export.h` uses macro guards generated by CMake's `GenerateExportHeader`). If a new implementation header follows the `helpers_impl.h` single-include pattern, document the intent with a comment at the top of the file.

---

### 3.2 Include style *(enforce)*

**Current practice (confirmed across all files):**

| Context | Style | Example |
|---------|-------|----------|
| Public Firebolt header, from any file | Double-quoted with `firebolt/` prefix | `"firebolt/types.h"`, `"firebolt/gateway.h"` |
| Local `src/` header, from within `src/` | Double-quoted, no path prefix | `"transport.h"`, `"utils.h"` |
| Local `src/` header, from within `test/` | Double-quoted, no path prefix | `"helpers_impl.h"` |
| Third-party library | Angle brackets | `<nlohmann/json.hpp>`, `<websocketpp/client.hpp>` |
| Standard library | Angle brackets | `<string>`, `<mutex>`, `<future>` |

**Rule**: Never use angle brackets for Firebolt-internal headers. Never use double-quotes for third-party or standard-library headers. This distinction makes the dependency boundary visually clear at every `#include` line.

Evidence: `src/gateway.cpp` includes `"firebolt/gateway.h"`, `"transport.h"`, `"utils.h"` with double quotes, and `<chrono>`, `<mutex>`, `<nlohmann/json.hpp>` with angle brackets.

---

### 3.3 Include order *(adopt going forward)*

For `.cpp` files, include in this order with a blank line between each group:
1. The file's own companion header (e.g., `"firebolt/gateway.h"` in `gateway.cpp`)
2. Other Firebolt public headers (`"firebolt/logger.h"`, etc.)
3. Local `src/` headers (`"transport.h"`, `"utils.h"`)
4. System and third-party headers (`<nlohmann/json.hpp>`, `<thread>`, `<map>`, etc.)

Evidence: `src/gateway.cpp` follows this ordering. This matches the Google C++ style include order and prevents transitive-include masking.

---

## 4. Class Structure and Copy/Move Semantics

### 4.1 Explicitly delete copy AND move for resource-owning classes *(adopt going forward)*

`Transport` is the reference implementation for correct deletion:

```cpp
// src/transport.h
Transport(const Transport&) = delete;
Transport(Transport&&) = delete;
Transport& operator=(const Transport&) = delete;
Transport& operator=(Transport&&) = delete;
```

**Rule**: Any class that owns a `std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`, a non-owning reference member (`T&`), or any non-copyable sub-object MUST explicitly delete copy AND move constructor AND assignment operator. Implicit suppression by the compiler is not sufficient — explicit deletion documents intent and prevents accidental use at call sites.

**Current inconsistency**: `HelperImpl` (in `src/helpers_impl.h`) holds `Firebolt::Transport::IGateway& gateway_` and a `std::mutex` but does not explicitly delete copy or move. `GatewayImpl` (in `src/gateway.cpp`) contains `Transport`, `std::thread`, and `std::mutex` members but also lacks explicit deletion. Both are implicitly non-copyable due to their members, but the intent is not documented.

> **[MAINTAINER CONFIRMATION NEEDED — item E]** Should `HelperImpl` and `GatewayImpl` explicitly delete copy and move? This would document intent without changing observable behaviour.

---

### 4.2 RAII helper classes must delete copy and move *(adopt going forward)*

`SubscriptionManager` is an RAII resource manager: its destructor calls `unsubscribeAll()`. It correctly deletes copy:

```cpp
// include/firebolt/helpers.h
SubscriptionManager(const SubscriptionManager&) = delete;
SubscriptionManager& operator=(const SubscriptionManager&) = delete;
```

Move is not yet deleted. A moved-from `SubscriptionManager` would call `unsubscribeAll()` on destruction (idempotent but surprising). `Logger` also deletes copy but not move.

**Rule**: Any RAII class that calls cleanup in its destructor (e.g., `unsubscribeAll()`, `disconnect()`) must delete both copy AND move. Copying produces two objects that both attempt to clean up the same resource. Moving without proper transfer semantics leaves the moved-from destructor running cleanup on a resource the moved-to object now "owns."

> **[MAINTAINER CONFIRMATION NEEDED — item F]** Should `SubscriptionManager` explicitly delete move operations? The current state (copy deleted, move not) is safe in practice but inconsistent with the rule above.

---

### 4.3 Destructor declaration *(enforce)*

**Current practice (confirmed across all classes):**
- Use `= default` unless the destructor does real cleanup work. Do not write `{ }` — an empty body and `= default` are not semantically identical in all contexts.
- Interface destructors (`~IGateway()`, `~IHelper()`) are declared in the header and defined as `= default` in the corresponding `.cpp` (confirmed: `IGateway::~IGateway() = default;` in `src/gateway.cpp`).
- `HelperImpl::~HelperImpl()` has a real body: it acquires the mutex and unsubscribes all active subscriptions. This is correct.
- `Server::~Server()` has a real body: calls `stopNotificationWorker()` then clears the event list. This is correct.
- `Transport::~Transport()` is declared `virtual` in the header and defined in `transport.cpp`.

**Rule**: Define a destructor with a body only when it does real cleanup work. Mark derived-class destructors `override`. Do not add `virtual` to a destructor in a final class.

---

## 5. Memory Ownership and Smart Pointer Usage

### Rationale
The codebase mixes smart pointers and raw pointers deliberately. The raw pointer usage for subscription callbacks is load-bearing and cannot be naively replaced.

---

### 5.1 `std::unique_ptr` for single-owner heap objects *(enforce)*

```cpp
// src/transport.h
std::unique_ptr<websocketpp::client<websocketpp::config::asio_client>> client_;

// test/unit/gatewayTest.cpp
std::unique_ptr<std::thread> m_serverThread;
```

**Rule**: Heap objects with a single clear owner use `std::unique_ptr`. Prefer brace-initialized (`std::make_unique<T>(...)`) over `reset(new T(...))`. The single exception is `Transport::start()` which uses `connectionThread_.reset(new ...)` due to websocketpp's own `shared_ptr`-based thread type — do not replicate this pattern.

---

### 5.2 `std::shared_ptr` for multi-owner objects *(enforce)*

```cpp
// src/gateway.cpp (Client class)
std::map<MessageID, std::shared_ptr<Caller>> queue;
```

`Caller` is shared because a response may arrive after the request queue has been pruned (timeout path). The websocketpp connection thread (`connectionThread_`) is also a `websocketpp::lib::shared_ptr<websocketpp::lib::thread>` because websocketpp owns the thread type.

**Rule**: Use `std::shared_ptr` only when the lifetime genuinely requires shared ownership. If you think you need `shared_ptr` for a new type, first confirm it cannot use `unique_ptr` or a reference.

---

### 5.3 `void*` for subscription callback user-data *(enforce — do not replace)*

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

### 5.4 References for constructor-injected dependencies *(enforce)*

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

### 5.5 No raw `new`/`delete` in new code *(enforce)*

Production code avoids direct `new`/`delete`; the only current exception is `src/transport.cpp` (`connectionThread_.reset(new websocketpp::lib::thread(...))`) due to websocketpp's non-movable thread type requiring `shared_ptr` ownership. All other heap allocation uses smart pointers or STL containers.

**Rule**: Never introduce *additional* raw `new`/`delete` in `src/` or `include/`. In tests, use `std::make_unique` even for short-lived objects.

---

## 6. Threading and Async Patterns

### Rationale
The transport has three concurrent threads in steady state: the websocketpp I/O thread, a message-dispatch worker thread, and a notification worker thread. A watchdog thread also runs. All inter-thread communication uses queues with condition variables — never direct cross-thread calls.

---

### 6.1 Worker threads use atomic stop-flag + condition variable *(enforce)*

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

### 6.2 Websocketpp callbacks dispatch to a queue; never execute directly *(enforce)*

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

### 6.3 `std::lock_guard` for simple mutex sections, `std::unique_lock` for condition variable wait *(enforce)*

```cpp
// src/gateway.cpp
std::lock_guard lck(eventMap_mtx);                     // simple section, CTAD OK
std::lock_guard<std::mutex> lock(notificationQueueMutex_);  // simple section, explicit type OK
std::unique_lock<std::mutex> lock(notificationQueueMutex_); // only when needed for cv.wait()
```

Both CTAD (`lock_guard lck(mtx)`) and explicit (`lock_guard<std::mutex> lock(mtx)`) are used. Either is acceptable.

**Rule**: Use `std::lock_guard` for all simple locked sections. Use `std::unique_lock` only when the lock needs to be passed to a condition variable `.wait()`. Never use `std::unique_lock` where `std::lock_guard` suffices.

---

### 6.4 `std::future` / `std::promise` for request/response correlation *(enforce)*

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

### 6.5 Watchdog is a polling thread; keep its interval configurable *(enforce)*

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

## 7. Error Handling

### Rationale
This library is a transport layer used by production embedded SDKs. Exceptions cannot propagate across the public ABI. All error paths must be observable by the caller without crashing or throwing.

---

### 7.1 Public APIs return `Firebolt::Error` or `Result<T>` — never throw *(enforce)*

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

### 7.2 `Result<T>` usage contract *(enforce)*

```cpp
// Check: if (result)
// Dereference: *result  or  result->field
// Error: result.error()
// Error info: result.errorInfo()
```

`Result<T>` has `operator bool()` that returns `true` iff `has_value()` is true (i.e., `Error::None`). The `value()` / `operator*` / `operator->` accessors are only valid after the bool check.

**Rule**: Always gate dereference with the bool check. Never call `*result` or `result->` without first checking `if (result)`. For `Result<void>`, `operator bool()` checks `error_ == Error::None` — use it the same way.

---

### 7.3 JSON parse errors: catch, log, return `Error::InvalidParams` *(enforce)*

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

**Rule**: Always catch JSON parse exceptions at the earliest boundary where the error can be mapped to a `Firebolt::Error`. Use `catch (const std::exception& e)` when logging `e.what()`; otherwise `catch (const std::exception&)` (unnamed form) is acceptable. In the transport layer (where there is no `Result<>` return), log and discard. In the helper/gateway layer, map to `Error::InvalidParams`.

---

### 7.4 `assert()` for debug-only precondition checks *(enforce)*

```cpp
// src/gateway.cpp
assert(onConnectionChange != nullptr);
assert(onMessage != nullptr);
```

**Rule**: Use `assert()` only to document invariants that must hold at development time and that cannot fail in a correctly-integrated production deployment. Do not use `assert()` for runtime input validation (e.g., a URL that could be empty). For user-facing precondition failures, return `Firebolt::Error::InvalidParams` instead.

---

### 7.5 Error propagation from websocketpp: use `mapError()` *(enforce)*

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

## 8. Logging

### Rationale
The logger writes to stderr (or syslog if `ENABLE_SYSLOG` is set). Log format is configurable at runtime via `Logger::setFormat()`. Log level is set at connect time via `Config::log.level`. All log calls in production code use the macros — this is consistent across the entire codebase.

---

### 8.1 Always use `FIREBOLT_LOG_*` macros *(enforce)*

```cpp
FIREBOLT_LOG_ERROR("Gateway", "Invalid payload received: %s", message.dump().c_str());
FIREBOLT_LOG_WARNING("Gateway", "No subscribers for event: %s", method.c_str());
FIREBOLT_LOG_NOTICE("Transport", "Version: %s", Version::String);
FIREBOLT_LOG_INFO("Gateway", "[disconnect] transport.disconnect() done in %ld ms, status=%d", ...);
FIREBOLT_LOG_DEBUG("Transport", "Received: %s", jsonMsg.dump().c_str());
```

**Rule**: Never use `std::cout`, `printf`, `fprintf`, or any direct output call in `src/` or `include/`. All diagnostic output goes through the `FIREBOLT_LOG_*` macros. These macros capture `__FILE__`, `__func__`, and `__LINE__` automatically.

---

### 8.2 Log level semantics *(enforce)*

| Level | When to use | Examples from codebase |
|-------|-------------|------------------------|
| `Error` | Unexpected state or failure that will affect functionality | Parse failure on incoming payload; failure to send message; invalid JSON-RPC payload |
| `Warning` | Transient issue the caller may recover from; ignorable in normal operation | Request timed out in watchdog; notification received while stopping; no subscriber for event |
| `Notice` | Important lifecycle event worth always seeing | Transport version; connect with URL; legacy RPCv1 mode |
| `Info` | Timing data and lifecycle step completions | Thread join timing; disconnect step timing |
| `Debug` | Internal state transitions useful only for debugging | Message send/receive content; subscribe/unsubscribe ACK timing; worker thread start/stop |

**Rule**: Do not use `Error` level for expected recoverable paths (e.g., subscribe ACK timeout during shutdown). Do not use `Debug` level for data that appears in every request path at production volumes — it would flood the log.

---

### 8.3 Printf-style format strings; never concatenate strings in log calls *(enforce)*

```cpp
// Correct
FIREBOLT_LOG_DEBUG("Gateway", "[subscribe] ACK for '%s' timed out after %lld ms", event.c_str(), ms);

// Wrong — do not do this
FIREBOLT_LOG_DEBUG("Gateway", std::string("[subscribe] ACK for '") + event + "' timed out");
```

**Rule**: The format argument to `FIREBOLT_LOG_*` must be a string literal. Never pass a `std::string` or a concatenated string as the format argument. This is enforced by `__attribute__((format(printf, 6, 7)))` on `Logger::log()` — the compiler will warn on format/argument mismatches.

---

### 8.4 Never log header values at non-Debug levels *(enforce)*

```cpp
// src/transport.cpp — onOpen
const auto& headers = con->get_response().get_headers();
for (const auto& header : headers)
{
    responseHeaders_[header.first] = header.second;
}
```

Response headers are stored silently. They are only readable by callers via `getResponseHeader()`.

**Rule**: If response headers ever need to be logged for debugging, log only the header name (never the value) and only at Debug level. Never log header values at any level — headers may contain authentication tokens. (Currently no header logging exists in production code — this rule prevents a future AI-introduced regression.)

---

## 9. Testing Patterns

### Rationale
The test suite mixes pure unit tests (mocked interfaces) and integration tests (live local websocketpp server). The integration tests are the primary coverage path for `Transport` and `GatewayImpl`. Coverage is measured with gcovr and uploaded as CI artifacts.

---

### 9.1 New test files: `test/unit/*Test.cpp` *(enforce)*

All unit test files follow the `*Test.cpp` naming pattern:
`gatewayTest.cpp`, `helperTest.cpp`, `transportTest.cpp`, `loggerTest.cpp`, `json_typesTest.cpp`.

The CMakeLists.txt glob is `file(GLOB UNIT_TESTS CONFIGURE_DEPENDS unit/*Test.cpp)`.

**Rule**: New test files must be named `<Component>Test.cpp` and placed in `test/unit/`. The CMake glob picks them up automatically. Do not add test files directly to `test/` root.

---

### 9.2 Test fixture names: `<Component>UTest` *(enforce)*

```cpp
class GatewayUTest : public ::testing::Test { ... };
class TransportUTest : public ::testing::Test { ... };
class HelperUTest : public ::testing::Test { ... };
class LoggerFormatUTest : public ::testing::Test { ... };
```

**Rule**: Test fixture class names use `<Component>UTest` suffix. Free-function tests (no fixture) use `TEST(<SuiteUTest>, <TestName>)` with the same naming pattern.

---

### 9.3 Unit tests mock interfaces with GMock *(enforce)*

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

### 9.4 Integration tests spin up a local websocketpp server *(enforce)*

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

Ports for the main test fixture classes: `9002` (`TransportIntegrationUTest` and `TransportTlsIntegrationUTest` in `transportTest.cpp`), `9003` (`GatewayUTest` in `gatewayTest.cpp`).

**Dynamic port allocation** (preferred for new test fixtures): Some tests in `gatewayTest.cpp` use `ReservedLoopbackPort reservedPort = reserveLikelyUnusedLoopbackPort()`, which binds port `0`, lets the OS assign a port, then holds the socket open until the test server starts on that port. This eliminates flaky failures from port collisions on busy CI machines. New integration test fixtures should follow this pattern instead of hardcoding ports.

**Rule**: Each integration test fixture must use a distinct hardcoded port OR the `reserveLikelyUnusedLoopbackPort()` dynamic-allocation pattern. Never share a port between two concurrently-running test fixtures. Call `GetGatewayInstance().disconnect()` in `TearDown()` before stopping the server. Always check `m_serverThread->joinable()` before calling `join()`.

---

### 9.5 Async synchronization uses `std::promise` with a timeout, never bare sleep *(adopt going forward)*

```cpp
// test/unit/gatewayTest.cpp
auto status = connectionFuture.wait_for(std::chrono::seconds(2));
ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
```

**Rule**: Avoid `std::this_thread::sleep_for` in tests to wait for asynchronous events — several existing tests use it as a timing hedge and are tracked as cleanup. New tests must use a `std::promise`/`std::future` pair with `wait_for()` and an `ASSERT_EQ(status, std::future_status::ready)` guard; if the timeout fires, the test must fail with a clear message.

---

### 9.6 Logger tests capture stderr with `pipe()`/`dup2()` *(enforce)*

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

### 9.7 Comment header block above each test *(adopt going forward)*

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

## 10. JSON Types and Serialization

### Rationale
`nlohmann::json` is the only JSON library used and is non-negotiable (it is a fixed dependency in `.github/Dockerfile`). The `Firebolt::JSON` namespace provides a thin wrapper layer that maps JSON values to C++ types via a uniform `fromJson()` / `value()` interface. Downstream SDKs depend on this interface shape.

---

### 10.1 All JSON serialization uses `nlohmann::json` *(enforce)*

```cpp
// include/firebolt/gateway.h
#include <nlohmann/json.hpp>
using EventCallback = std::function<void(void* usercb, const nlohmann::json& params)>;
```

**Rule**: Do not introduce any other JSON library. Do not use `nlohmann::json` via the `nlohmann_json_schema_validator` path for anything other than schema validation in tests (it is a test-only dependency per `test/CMakeLists.txt`).

---

### 10.2 JSON type wrappers: `fromJson()` + `value()` interface *(enforce)*

```cpp
// include/firebolt/json_types.h
template <typename T> class NL_Json_Basic {
public:
    virtual void fromJson(const nlohmann::json& json) = 0;
    virtual T value() const = 0;
};

using String = BasicType<std::string>;
using Boolean = BasicType<bool>;
using Float = BasicType<float>;
using Unsigned = BasicType<uint32_t>;
using Integer = BasicType<int32_t>;
```

**Rule**: New JSON-mapped types in `Firebolt::JSON` must inherit from `NL_Json_Basic<T>` and implement `fromJson()` and `value()`. Do not create ad-hoc structs that manually parse `nlohmann::json` without going through this interface — consistency here is required for the `IHelper::get<JsonType, PropertyType>()` template to work correctly.

**Cleanup**: `include/firebolt/json_types.h` declares `T virtual value() const = 0;` in the `NL_Json_Basic` base class. This ordering is valid standard C++ (`virtual` is a `decl-specifier` and may appear before or after the return type) but is unconventional and misleading to readers. Prefer `virtual T value() const = 0;` for consistency and readability. Correct this in a follow-on PR.

---

### 10.3 Let `nlohmann::json::get<T>()` throw on type mismatch *(enforce)*

```cpp
// include/firebolt/json_types.h — BasicType
void fromJson(const nlohmann::json& json) override { value_ = json.get<T>(); }

// test/unit/json_typesTest.cpp
EXPECT_THROW(str.fromJson(jsonInt), nlohmann::json::type_error);
```

**Rule**: Do not add `try/catch` inside `fromJson()` implementations. Let `nlohmann::json::type_error` propagate to the caller (`IHelper::get<>()` or `onPropertyChangedCallback<>()`), which has a uniform catch block that logs the error and returns `Error::InvalidParams`.

---

### 10.4 Use `.at()` not `[]` for mandatory field access *(adopt going forward)*

```cpp
// test/unit/helperTest.cpp — TestJson
void fromJson(const nlohmann::json& json) { v = json.at("value").get<int>(); }
```

`.at()` throws `nlohmann::json::out_of_range` on missing key. `[]` silently inserts a null value.

**Rule going forward**: When implementing `fromJson()` for a struct that requires a field to be present, use `.at("field")`, not `["field"]`. Optional fields may use `json.contains("field")` + `json["field"]`.

---

### 10.5 `// clang-format off` is permitted for JSON-keyed initializers *(enforce)*

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

## 11. Build System and Tooling

### Rationale
The build environment is hermetic: all library dependencies live inside the Docker image built from `.github/Dockerfile`. The wrapper scripts in the repo root abstract Docker invocation, image-build-on-demand, and environment setup. Bypassing them produces a binary that diverges from CI and cannot be trusted for regression validation.

---

### 11.1 Use wrapper scripts only; never invoke cmake directly on the host *(enforce)*

| Script | Purpose |
|--------|---------|
| `./test.sh` | Build all targets and run the full unit test suite. Run before every commit. |
| `./fmt.sh` | clang-format check (dry-run, `--Werror`). Mirrors the CI format gate exactly. |
| `./fmt.sh --fix` | Reformat all `src/` and `include/` sources in-place. |
| `./build.sh +tests` | Manual CMake configure and build with tests enabled. Requires the `SYSROOT_PATH` environment variable. |
| `./cov_build.sh` | Submit a Coverity cloud scan (requires Coverity credentials). |
| `./coverity_local.sh` | Run a local Coverity static analysis pass. |
`./test.sh` and `./fmt.sh` build the Docker image (`firebolt-cpp-transport-ci:local`) automatically on first run from `.github/Dockerfile`; subsequent invocations reuse the cached image.

**Rule**: Never run `cmake`, `make`, or any equivalent tool directly on the host machine. All production dependencies (`nlohmann_json`, `websocketpp`, `GTest`, `gcovr`) are installed only inside the Docker image. A host-side configure may silently succeed if those libraries are incidentally present, producing a binary that passes locally but fails or behaves differently in CI.

Evidence: `.github/Dockerfile` installs all deps into the image only. `README.md` documents no host-side install path for the development cycle.

**Cleanup**: If `build-dev/` was created by a host-side cmake, delete it with `sudo rm -rf build-dev/` and regenerate via `./test.sh`. Do not commit any file under `build-dev/` — it is covered by `.gitignore`.

---

### 11.2 Build directory ownership and cache validity *(enforce)*

`build-dev/` is created inside the Docker container and is owned by `root` from the host filesystem's perspective.

**Rule**: If any host-side operation fails with a permission error against `build-dev/`, run `sudo rm -rf build-dev/` and re-run `./test.sh`. Do not attempt to `chown` the directory — the next Docker invocation recreates it correctly.

`build-dev/CMakeCache.txt` bakes the Docker-internal source path (`/workspace`) into the cache at configure time. If the cache was created in a different build context (e.g., the directory was moved or the image rebuilt), `test.sh` detects the stale path and wipes the cache automatically before reconfiguring. This wipe is intentional, not a bug.

**Rule**: Do not copy a `CMakeCache.txt` between machines or Docker contexts. The baked-in source path will be wrong and will cause a misconfigured build that appears to succeed.

---

## 12. Anti-Patterns Catalogue

The following patterns are explicitly wrong for this codebase. Each entry notes the risk source.

| # | Anti-Pattern | Why It Is Wrong Here |
|---|---|---|
| AP-1 | Adding `<websocketpp/...>` or `<boost/...>` includes to `include/firebolt/` | Public ABI must not expose transport-layer dependencies; downstream SDKs would acquire a transitive dependency on websocketpp |
| AP-2 | Adding any third-party include to `include/firebolt/` beyond `<nlohmann/json.hpp>` | `nlohmann::json` is the sole approved exception (it is the public wire-format type); all others violate §1.1 |
| AP-3 | Calling `messageReceiver_` or `connectionReceiver_` directly from a websocketpp handler | Handlers run on the I/O thread; calling the receiver directly risks deadlock if the receiver tries to acquire a mutex held by the I/O thread — always enqueue instead (§6.2) |
| AP-4 | Using `std::this_thread::sleep_for` in worker threads (non-watchdog) | Worker threads must wait with a condition variable predicate, not sleep; `sleep_for` in a worker causes fixed-latency wake even when the queue is already full |
| AP-5 | Using `std::unique_lock` where `std::lock_guard` suffices | `unique_lock` has overhead; reserve it exclusively for condition-variable `.wait()` calls (§6.3) |
| AP-6 | Returning `bool` for success/failure from a public API method | All public methods return `Firebolt::Error` or `Result<T>`; a bare `bool` loses the error code |
| AP-7 | Replacing `void*` in `EventCallback` or `IGateway::subscribe()` with a typed pointer or `std::any` | The `void*`/`reinterpret_cast` pair is the established public ABI; downstream SDKs depend on the exact signature |
| AP-8 | Adding `catch` blocks inside `fromJson()` implementations | Let `nlohmann::json::type_error` propagate to `IHelper::get<>()` which has the uniform catch-and-log handler (§10.3) |
| AP-9 | Logging response header values at any level, or logging header names above Debug level | Headers may contain authentication tokens; if debugging requires it, log only the header name at Debug level — never the value at any level (§8.4) |
| AP-10 | Introducing raw `new`/`delete` in `src/` or `include/` | All heap allocation uses smart pointers or STL containers (§5.5) |
| AP-11 | Spinning up a real WebSocket connection in a unit test (non-integration fixture) | Mock `IGateway` with GMock instead; real connections belong in integration fixtures (`GatewayUTest`, `TransportIntegrationUTest`) |
| AP-12 | Using `auto` for short named types | `auto err`, `auto result`, `auto id` silently mask type mismatches; spell out `Firebolt::Error`, `Result<T>`, `SubscriptionId` (§2.11) |
| AP-13 | Declaring a new third-party dependency in `src/CMakeLists.txt` without first adding it to `.github/Dockerfile` | The Dockerfile is the authoritative approved-dependency list; a PR that adds the include without the Dockerfile entry passes locally but fails in CI (§1.7) |
| AP-14 | Making a copy of `void* usercb` outside of the `HelperImpl::subscribe()` / `unsubscribe()` pair | The pointer is the address of a `std::map` entry; it is only valid while that entry exists in `HelperImpl::subscriptions_` |

---

## 13. Assumptions and Open Questions

### 13.1 Explicit assumptions made during analysis

These conventions are observed consistently but are not documented in any existing file. They are treated as binding rules in this document. If a maintainer disagrees, the relevant rule should be revised.

| # | Assumption | Evidence |
|---|-----------|----------|
| A1 | `GatewayImpl` is the only concrete implementation of `IGateway` in this repo. A second implementation (e.g., TLS) is not expected. | No `#ifdef` or factory-dispatch logic in `GetGatewayInstance()`. |
| A2 | The `void* usercb` / `void* notificationPtr` pattern for subscription callbacks is intentional ABI design, not a historical accident. | The address-of-map-entry pattern in `HelperImpl::subscribe()` and the typed cast in `onPropertyChangedCallback<>()` form a consistent pair. |
| A3 | `legacyRPCv1` mode is a maintenance concern only. New features must target RPC v2 (default). | The `ENABLE_LEGACY_RPC_V1=ON` CMake option is an explicit opt-in; its `false` default and the `RPCv2=true` query parameter in `buildGatewayUrl()` confirm v2 is the primary mode. |
| A4 | `Config` is a value type passed by value to `connect()`. No part of the library retains a reference to the caller's `Config`. | `connect()` signature is `connect(const Firebolt::Config& cfg, ...)` and all relevant fields are copied to local statics or members. |
| A5 | The two test ports (9002, 9003) are assumed unused on the CI machine during tests. No dynamic port allocation is used. | Hardcoded in `transportTest.cpp` and `gatewayTest.cpp`. |

---

### 13.2 Flagged items requiring maintainer confirmation before treating as binding

| Tag | Rule reference | Question |
|-----|----------------|----------|
| **[A]** | §2.2 | Should `NL_Json_Basic` be renamed `INL_Json_Basic` to follow the interface prefix convention? |
| **[B]** | §2.4 | Is a bulk rename of non-underscore private members in `GatewayImpl`/`Client`/`Server` in scope for a near-term PR? |
| **[C]** | §2.5 | Should `runtime_waitTime_ms` and `watchdog_interval_ms` be converted to `constexpr` constants (or at least renamed to `k*`)? Currently they are written during `connect()` from `Config`, so they are not compile-time constants. Clarify intended mutability. |
| **[D]** | §5.3 | **Partially answered**: The synchronous unsubscribe paths (`HelperImpl::unsubscribe()`, `unsubscribeAll()`, `~HelperImpl()`) are safe — `gateway_.unsubscribe()` is always called before the map entry is erased, and `Server::unsubscribe()` only compares the `void*` for equality and never dereferences it. The open question is the **notification-queue race**: if `Server::notify()` already enqueued a `QueuedNotification` (copying callbacks to a local vector) before `unsubscribeAll()` ran, the notification worker thread may call `lambda(usercb, params)` after the map entry has been erased, dereferencing a freed `void*`. Confirm whether the expected lifecycle guarantees that all in-flight event delivery completes before any subscription teardown (e.g., is `unsubscribeAll()` always called from a path that is serialized with respect to notification delivery?). |
| **[E]** | §4.1 | Should `HelperImpl` and `GatewayImpl` explicitly delete copy and move operations? Both are implicitly non-copyable (reference members, non-copyable sub-objects), but explicit `= delete` would document intent and prevent future maintenance mistakes. |
| **[F]** | §4.2 | Should `SubscriptionManager` delete move operations? It is an RAII resource manager; a moved-from instance would call `unsubscribeAll()` on destruction, which is idempotent but potentially surprising. Deleting move prevents ambiguous ownership patterns. |

---

*Last updated: 2026-07-16. Derived from direct analysis of: `include/firebolt/gateway.h`, `include/firebolt/helpers.h`, `include/firebolt/json_types.h`, `include/firebolt/types.h`, `include/firebolt/logger.h`, `include/firebolt/config.h.in`, `src/gateway.cpp`, `src/transport.h`, `src/transport.cpp`, `src/helpers_impl.h`, `src/helpers_impl.cpp`, `src/utils.h`, `src/utils.cpp`, `src/logger.cpp`, `test/unit/gatewayTest.cpp`, `test/unit/helperTest.cpp`, `test/unit/transportTest.cpp`, `test/unit/loggerTest.cpp`, `test/unit/json_typesTest.cpp`, `CMakeLists.txt`, `src/CMakeLists.txt`, `test/CMakeLists.txt`, `.clang-format`, `.github/Dockerfile`.*

---

## 14. Relationship to Other Policy Files

This document is the **single authoritative source** for all coding conventions, architecture rules, testing patterns, and build tooling guidance in `firebolt-cpp-transport`. All rules previously in `.github/copilot-instructions.md` are either covered here or are intentionally absent because they are not coding conventions (CI branch targets, release tag format).

| File | Scope |
|------|-------|
| `.github/instructions/coding-guidelines.instructions.md` *(this file)* | Coding conventions, architecture, testing patterns, build tooling |
| `CONTRIBUTING.md` | Contribution process (CLA, PR workflow) |
| `.clang-format` | Whitespace, indentation, brace style — enforced by CI; rules not restated here |
| `.github/Dockerfile` | Authoritative list of approved build and test dependencies |

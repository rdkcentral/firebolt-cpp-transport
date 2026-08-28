## [1.1.12.rc1](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.11...v1.1.12.rc1)

### Changed

- Refactored the Gateway watchdog to use `std::condition_variable::wait_for()` instead of `std::this_thread::sleep_for()`.
- Preserved existing watchdog timeout monitoring behavior for pending requests.
- Gateway disconnect now immediately wakes the watchdog thread, eliminating watchdog-induced teardown delays.
- No Firebolt API contract or public API changes.

## [1.1.11](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.10...v1.1.11)

### Added
- Configurable retry policy for initial gateway connection (`Config.reconnect_max_attempts`, `reconnect_delay_ms`, `connect_attempt_timeout_ms`) for embedded devices where the gateway may not be ready at startup
- `test-soc.sh` to run integration tests against a physical device over SSH tunnel
- `test-no-network.sh` integration test for no-server failure paths
- WebSocket++ vendored in-tree; excluded from BlackDuck scan via `.bdignore`

### Fixed
- Subscription notification race: stale callbacks after `unsubscribe()` are silently dropped via a `std::weak_ptr` guard instead of invoking through a dangling pointer
- `disconnect()` now aborts in-flight connections (DNS/TCP/TLS) promptly via a new `Connecting` transport state; previously it could hang waiting for the attempt to complete
- `connect()` returns `AlreadyConnected` when a connection is already in-progress, not just when fully connected
- `connectionChangeListener` protected by mutex, null-checked before invocation, and restored atomically if `connect()` fails
- Custom header injection wrapped in try/catch; returns `Error::NotConnected` on failure
- Logger maps made `const`; all lookups use `find()` to avoid silent map insertions on unknown log levels
- `watchdog_interval_ms` now correctly initialised from `cfg.watchdogCycle_ms` in `connect()`
- JSON-RPC message ID handling hardened against non-unsigned integer ID values
- Removed unresolved MIT license placeholder from `LICENSE`

## [1.1.11.rc1](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.10...v1.1.11.rc1)

### Added
- Configurable retry policy for initial gateway connection (`Config.reconnect_max_attempts`, `reconnect_delay_ms`, `connect_attempt_timeout_ms`) for embedded devices where the gateway may not be ready at startup
- `test-soc.sh` to run integration tests against a physical device over SSH tunnel
- `test-no-network.sh` integration test for no-server failure paths
- WebSocket++ vendored in-tree; excluded from BlackDuck scan via `.bdignore`

### Fixed
- Subscription notification race: stale callbacks after `unsubscribe()` are silently dropped via a `std::weak_ptr` guard instead of invoking through a dangling pointer
- `disconnect()` now aborts in-flight connections (DNS/TCP/TLS) promptly via a new `Connecting` transport state; previously it could hang waiting for the attempt to complete
- `connect()` returns `AlreadyConnected` when a connection is already in-progress, not just when fully connected
- `connectionChangeListener` protected by mutex, null-checked before invocation, and restored atomically if `connect()` fails
- Custom header injection wrapped in try/catch; returns `Error::NotConnected` on failure
- Logger maps made `const`; all lookups use `find()` to avoid silent map insertions on unknown log levels
- `watchdog_interval_ms` now correctly initialised from `cfg.watchdogCycle_ms` in `connect()`
- JSON-RPC message ID handling hardened against non-unsigned integer ID values
- Removed unresolved MIT license placeholder from `LICENSE`

## [1.1.8](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.7...v1.1.8)

### Added
- Timing and diagnostic log messages throughout `disconnect()`, `subscribe()`, and `unsubscribe()` in Gateway and Transport to aid debugging of latency issues

## [1.1.7](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.6...v1.1.7)

### Fixed
- `disconnect()` could block callers for up to ~5 seconds when the gateway was unresponsive but the TCP connection remained open; the WebSocket close-handshake timeout is now capped at 100 ms via `set_close_handshake_timeout()` before calling `close()`

## [1.1.6](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.5...v1.1.6)

### Changed
- Allow building without a SONAME

## [1.1.5](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.4...v1.1.5)

### Changed
- Enable websocketpp logs by default only at `Debug` log level; they can also be controlled with
  `Config.log.transportInclude` and `Config.log.transportExclude`

### Fixed
- In legacy protocol, allow `result` to be an object or an array

## [1.1.4](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.3...v1.1.4)

### Fixed
- Add `/` at the end of URL, otherwise query arguments were not sent

## [1.1.3](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.2...v1.1.3)

### Changed
- Add JSON payload validation to ensure all required fields are present

## [1.1.2](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.1...v1.1.2)

### Changed
- Allow setting log level to `MaxLevel`, falling back to `Debug`

### Fixed
- Events are dispatched in separate thread to avoid blocking the main queue

## [1.1.1](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.0...v1.1.1)

### Fixed
- Deadlock when unsubscribing from an event inside its callback

## [1.1.0](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0...v1.1.0)

### Added
- Legacy support: Events can be retrieved in the old way via `legacyRPCv1` configuration option

### Changed
- Set `SameMajorVersion` compatibility

## [1.0.0](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v0.2.0...v1.0.0)

### Added
- Notice log level

### Changed
- **Breaking**: Configuration API for the Transport has been redesigned
- **Breaking**: The `invoke` and `request` methods are now asynchronous and no longer block waiting for responses
- **Breaking**: Header files have been moved to a `firebolt/` subdirectory - include paths must be updated (e.g., `#include <firebolt/config.h>`)
- **Breaking**: Function names now follow camelCase convention starting with lowercase letters
- **Breaking**: Event subscription string comparison is now case-sensitive
- **Breaking**: Removed dependency on Thunder
- Event payloads now require primitive values to be wrapped with a "value" key (e.g., `"params": { "value": VALUE }`)
- Set ExactVersion compatibility for dependencies

### Fixed
- Protection against incorrect payloads
- Protection for incorrect parameters in printf-style functions
- Client disconnect sequence
- Reconnection issue
- Race condition on simultaneous calls to Gateway::connect()

## [0.2.0](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v0.1.0...v0.2.0)

### Changed
- Communication with an endpoint is now JSON-RPC compliant

## [0.1.0](https://github.com/rdkcentral/firebolt-cpp-transport/compare/709d9c6...v0.1.0)

### Added
- Initial Transport with "unidirectional" communication with an endpoint

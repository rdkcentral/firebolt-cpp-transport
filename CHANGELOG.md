## [Unreleased](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.1.1...main)

### Changed
- Allow setting log level to `MaxLevel`, fallback to `Debug`

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

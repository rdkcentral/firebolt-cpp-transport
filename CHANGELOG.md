## [Unreleased](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.25...main)

## [1.0.0-next.25](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.24...v1.0.0-next.25) (2025-12-17)

No user-facing changes

## [1.0.0-next.24](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.23...v1.0.0-next.24) (2025-12-12)

### Changed
- Event payloads are now treated as primitive types only if they are JSON objects containing a single "value" key

## [1.0.0-next.23](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.22...v1.0.0-next.23) (2025-12-05)

No user-facing changes

## [1.0.0-next.22](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.21...v1.0.0-next.22) (2025-12-03)

No user-facing changes

## [1.0.0-next.21](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.20...v1.0.0-next.21) (2025-11-28)

No user-facing changes

## [1.0.0-next.20](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.19...v1.0.0-next.20) (2025-11-28)

### Changed
- **Breaking**: In event subscriptions, string comparison is now case-sensitive, reverting the behavior from `v1.0.0-next.1`

## [1.0.0-next.19](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.18...v1.0.0-next.19) (2025-11-26)

### Fixed
- Resolved an issue in the client disconnect sequence

### Changed
- Set ExactVersion compatibility for dependencies

## [1.0.0-next.18](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.17...v1.0.0-next.18) (2025-11-25)

No user-facing changes

## [1.0.0-next.17](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.16...v1.0.0-next.17) (2025-11-25)

### Added
- Notice log level

## [1.0.0-next.16](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.15...v1.0.0-next.16) (2025-11-25)

No user-facing changes

## [1.0.0-next.15](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.14...v1.0.0-next.15) (2025-11-24)

### Changed
- **Breaking**: Renamed functions to start with a lower-case letter

## [1.0.0-next.14](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.13...v1.0.0-next.14) (2025-11-20)

### Changed
- **Breaking**: The `invoke` method is now asynchronous and no longer waits for a response
- **Breaking**: Header files have been moved to a `firebolt/` subdirectory and include paths must be updated (e.g., `#include <firebolt/config.h>`)

## [1.0.0-next.13](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.12...v1.0.0-next.13) (2025-11-19)

### Changed
- **Breaking**: Header include paths must now be prefixed with `Firebolt/` (Note: This was superseded by the `firebolt/` change in `v1.0.0-next.14`)

## [1.0.0-next.12](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.11...v1.0.0-next.12) (2025-11-19)

No user-facing changes

## [1.0.0-next.11](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.10...v1.0.0-next.11) (2025-11-18)

### Changed
- Event payloads updated to agreed version

## [1.0.0-next.10](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.9...v1.0.0-next.10) (2025-11-17)

### Fixed
- Added protection for incorrect parameters for printf-style functions

## [1.0.0-next.9](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.8...v1.0.0-next.9) (2025-11-06)

### Fixed
- Added protection against incorrect payloads

## [1.0.0-next.8](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.7...v1.0.0-next.8) (2025-11-05)

No user-facing changes

## [1.0.0-next.7](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.6...v1.0.0-next.7) (2025-11-05)

### Changed
- Primitive objects in event payloads are now wrapped (e.g., `"params": { "value": VALUE }`)

## [1.0.0-next.6](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.5...v1.0.0-next.6) (2025-11-04)

### Changed
- **Breaking**: Updated the configuration for the Transport

## [1.0.0-next.5](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.4...v1.0.0-next.5) (2025-11-04)

No user-facing changes

## [1.0.0-next.4](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.3...v1.0.0-next.4) (2025-10-29)

### Changed
- Updated the initialization of the Transport

## [1.0.0-next.3](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.2...v1.0.0-next.3) (2025-10-27)

No user-facing changes

## [1.0.0-next.2](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v1.0.0-next.1...v1.0.0-next.2) (2025-10-27)

No user-facing changes

## [1.0.0-next.1](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v0.2.1-next.1...v1.0.0-next.1) (2025-10-23)

### Changed
- In event subscriptions, string comparison is now case-insensitive
- Removed dependency on Thunder

## [0.2.1-next.1](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v0.2.0...v0.2.1-next.1) (2025-10-23)

No user-facing changes

## [0.2.0](https://github.com/rdkcentral/firebolt-cpp-transport/compare/v0.1.0...v0.2.0) (2025-08-01)

### Changed
- Communication with an endpoint is now JSON-RPC compliant

## [0.1.0](https://github.com/rdkcentral/firebolt-cpp-transport/compare/709d9c6...v0.1.0) (2025-07-04)

### Added
- Initial Transport with "unidirectional" communication with an endpoint

# Firebolt C++ Specifics Spec

## Overview
This spec consolidates all C++-specific implementation details, patterns, and library choices from the Firebolt transport documentation. It is intended to separate C++-centric information from general transport and protocol specs for easier cross-language adoption.

---

## JSON Serialization & Deserialization
- Uses [nlohmann::json](https://github.com/nlohmann/json) for all JSON serialization/deserialization.
- Provides modern C++-style interface for working with JSON objects.
- Integrates with STL containers and custom types.

---

## Callback Patterns
- EventCallback: `std::function<void(void* usercb, const nlohmann::json& params)>`
- ResponseCallback: `std::function<void(Firebolt::Result<nlohmann::json>)>`
- Callbacks receive parsed nlohmann::json payloads.

---

## Interface Signatures
- IGateway request method:
  ```cpp
  virtual Firebolt::Error request(const std::string& method,
                                  const nlohmann::json& parameters,
                                  std::function<void(Firebolt::Result<nlohmann::json>)> responseCallback) = 0;
  ```
- IHelper property access:
  ```cpp
  Result<bool> result = helper_.get<Firebolt::JSON::Boolean, bool>("Discovery.watched", parameters);
  ```
- Enum serialization:
  ```cpp
  parameters["agePolicy"] = Firebolt::JSON::toString(Firebolt::JsonData::AgePolicyEnum, *agePolicy);
  ```

---

## RAII & Template Usage
- SubscriptionManager provides RAII cleanup and template-based notifications.
- Type-safe, template-based integration for client libraries.

---

## Error Handling & Result Types
- Uses types.h for Result and error handling.
- Consistent API contracts via C++ types.

---

## Library Choices
- nlohmann::json for JSON
- STL containers for data structures
- std::any for flexible payloads

---

## Notes
- All C++-specific code, patterns, and library references are consolidated here for clarity.
- General transport, protocol, and cross-language recommendations remain in their respective specs.

/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 Sky UK
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace Firebolt
{

enum class Error : int32_t
{
    None = 0,
    General = 1,
    Timedout = 2,
    NotConnected = 3,
    AlreadyConnected = 4,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    CapabilityNotAvailable = -50300,
    CapabilityNotSupported = -50100,
    CapabilityGet = -50200,
    CapabilityNotPermitted = -40300,
};

enum class LogLevel : uint8_t
{
    Error,
    Warning,
    Notice,
    Info,
    Debug,
    MaxLevel
};

using SubscriptionId = std::uint64_t;

template <typename T> class Result
{
public:
    explicit Result(const T& value)
        : value_{value},
          error_{Error::None}
    {
    }
    explicit Result(const Error& error)
        : value_{},
          error_{error}
    {
    }

    bool has_value() const { return value_.has_value(); }
    explicit operator bool() const { return has_value(); }
    T& value() & { return *value_; }
    const T& value() const& { return *value_; }
    const T* operator->() const { return &value(); }
    T* operator->() { return &value(); }
    const T& operator*() const& { return value(); }
    T& operator*() & { return value(); }
    T value_or(T&& defaultValue) const& { return value_.value_or(defaultValue); }
    const Error& error() const& { return error_; }
    Error& error() & { return error_; }

private:
    std::optional<T> value_;
    Error error_;
};

template <> class Result<void>
{
public:
    explicit Result(const Error& error)
        : error_{error}
    {
    }

    explicit operator bool() const { return Firebolt::Error::None == error_; }
    const Error& error() const& { return error_; }
    Error& error() & { return error_; }

private:
    Error error_;
};

namespace Types
{
using BooleanMap = std::unordered_map<std::string, bool>;
} // namespace Types
} // namespace Firebolt

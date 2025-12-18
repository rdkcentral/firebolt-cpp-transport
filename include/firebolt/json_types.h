/**
 * Copyright 2025 Comcast Cable Communications Management, LLC
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
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Firebolt::JSON
{

template <typename T> using EnumType = std::map<std::string, T>;
template <typename T> inline std::string toString(const EnumType<T>& enumType, const T& value)
{
    auto it = std::find_if(enumType.begin(), enumType.end(), [&value](const auto& pair) { return pair.second == value; });
    if (it != enumType.end())
    {
        return it->first;
    }
    return {};
}

template <typename T> class NL_Json_Basic
{
public:
    virtual void fromJson(const nlohmann::json& json) = 0;
    T virtual value() const = 0;
};

template <typename T> class BasicType : public NL_Json_Basic<T>
{
public:
    void fromJson(const nlohmann::json& json) override { value_ = json.get<T>(); }
    T value() const override { return value_; }

private:
    T value_;
};

using String = BasicType<std::string>;
using Boolean = BasicType<bool>;
using Float = BasicType<float>;
using Unsigned = BasicType<uint32_t>;
using Integer = BasicType<int32_t>;

template <typename T1, typename T2> class NL_Json_Array : public NL_Json_Basic<std::vector<T2>>
{
public:
    void fromJson(const nlohmann::json& json) override
    {
        value_.clear();
        if (!json.is_array())
        {
            throw nlohmann::json::type_error::create(302, "type must be array", nullptr);
        }
        for (const auto& item : json)
        {
            T1 element;
            element.fromJson(item);
            value_.push_back(element.value());
        }
    }
    std::vector<T2> value() const override { return value_; }

private:
    std::vector<T2> value_;
};

} // namespace Firebolt::JSON

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

#include "firebolt/logger.h"
#include "firebolt/transport_export.h"
#include "firebolt/types.h"
#include <any>
#include <nlohmann/json.hpp>
#include <string>
#include <tuple>

namespace Firebolt::Helpers
{

struct SubscriptionData
{
    void* owner;
    std::string eventName;
    std::any notification;
};

template <typename JsonType, typename... Args>
void onPropertyChangedCallback(void* subscriptionDataPtr, const nlohmann::json& jsonResponse)
{
    SubscriptionData* subscriptionData = reinterpret_cast<SubscriptionData*>(subscriptionDataPtr);
    auto notifier = std::any_cast<std::function<void(Args...)>>(subscriptionData->notification);
    JsonType jsonType;
    try
    {
        jsonType.fromJson(jsonResponse);
        if constexpr (sizeof...(Args) > 1)
        {
            std::apply(notifier, jsonType.value());
        }
        else
        {
            notifier(jsonType.value());
        }
    }
    catch (const std::exception& e)
    {
        FIREBOLT_LOG_ERROR("Event", "Cannot parse event data for event %s, payload: %s",
                           subscriptionData->eventName.c_str(), jsonResponse.dump().c_str());
    }
}

class IHelper
{
public:
    virtual ~IHelper() = default;

    virtual Result<void> set(const std::string& methodName, const nlohmann::json& parameters) = 0;
    virtual Result<void> invoke(const std::string& methodName, const nlohmann::json& parameters) = 0;
    template <typename JsonType, typename PropertyType>
    Result<PropertyType> get(const std::string& methodName, const nlohmann::json& parameters = nlohmann::json({}))
    {
        Result<nlohmann::json> result = getJson(methodName, parameters);
        if (!result)
        {
            return Result<PropertyType>{result.error()};
        }
        try
        {
            JsonType jsonResult;
            jsonResult.fromJson(*result);
            return Result<PropertyType>{jsonResult.value()};
        }
        catch (const std::exception& e)
        {
            FIREBOLT_LOG_ERROR("Getter", "Cannot parse data for a getter %s, payload: %s", methodName.c_str(),
                               result->dump().c_str());
            return Result<PropertyType>{Firebolt::Error::InvalidParams};
        }
    }

    virtual Result<SubscriptionId> subscribe(void* owner, const std::string& eventName, std::any&& notification,
                                             void (*callback)(void*, const nlohmann::json&)) = 0;
    virtual Result<void> unsubscribe(SubscriptionId id) = 0;
    virtual void unsubscribeAll(void* owner) = 0;

private:
    virtual Result<nlohmann::json> getJson(const std::string& methodName, const nlohmann::json& parameters) = 0;
};

class FIREBOLTTRANSPORT_EXPORT SubscriptionManager
{
public:
    SubscriptionManager(IHelper& helper, void* owner);
    ~SubscriptionManager();

    SubscriptionManager(const SubscriptionManager&) = delete;
    SubscriptionManager& operator=(const SubscriptionManager&) = delete;

    template <typename JsonType, typename... Args>
    Result<SubscriptionId> subscribe(const std::string& eventName, std::function<void(Args...)>&& notification)
    {
        return helper_.subscribe(owner_, eventName, std::move(notification),
                                 onPropertyChangedCallback<JsonType, Args...>);
    }

    Result<void> unsubscribe(SubscriptionId id);
    void unsubscribeAll();

private:
    IHelper& helper_;
    void* owner_;
};

FIREBOLTTRANSPORT_EXPORT IHelper& GetHelperInstance();

} // namespace Firebolt::Helpers

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

#include "firebolt/helpers.h"
#include "firebolt/gateway.h"

namespace Firebolt::Helpers
{

SubscriptionManager::SubscriptionManager(IHelper& helper, void* owner)
    : helper_(helper),
      owner_(owner)
{
}
SubscriptionManager::~SubscriptionManager()
{
    unsubscribeAll();
}

Result<void> SubscriptionManager::unsubscribe(SubscriptionId id)
{
    return helper_.unsubscribe(id);
}

void SubscriptionManager::unsubscribeAll()
{
    helper_.unsubscribeAll(owner_);
}

class HelperImpl : public IHelper
{
public:
    ~HelperImpl() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& subscription : subscriptions_)
        {
            void* notificationPtr = reinterpret_cast<void*>(&subscription.second);
            Firebolt::Transport::GetGatewayInstance().unsubscribe(subscription.second.eventName, notificationPtr);
        }
        subscriptions_.clear();
    }

    Result<void> set(const std::string& methodName, const nlohmann::json& parameters) override
    {
        nlohmann::json result;
        nlohmann::json p;
        if (parameters.is_object())
        {
            p = parameters;
        }
        else
        {
            p["value"] = parameters;
        }
        auto future = Firebolt::Transport::GetGatewayInstance().request(methodName, p);
        auto requestResult = future.get();

        if (!requestResult)
        {
            return Result<void>{requestResult.error()};
        }
        return Result<void>{Error::None};
    }

    Result<void> invoke(const std::string& methodName, const nlohmann::json& parameters) override
    {
        return Result<void>{Firebolt::Transport::GetGatewayInstance().send(methodName, parameters)};
    }

    Result<void> unsubscribe(SubscriptionId id) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(id);
        if (it == subscriptions_.end())
        {
            return Result<void>{Error::General};
        }
        void* notificationPtr = reinterpret_cast<void*>(&it->second);
        auto errorStatus{Firebolt::Transport::GetGatewayInstance().unsubscribe(it->second.eventName, notificationPtr)};
        subscriptions_.erase(it);
        return Result<void>{errorStatus};
    }

    void unsubscribeAll(void* owner) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();)
        {
            if (it->second.owner == owner)
            {
                void* notificationPtr = reinterpret_cast<void*>(&it->second);
                Firebolt::Transport::GetGatewayInstance().unsubscribe(it->second.eventName, notificationPtr);
                it = subscriptions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

private:
    Result<nlohmann::json> getJson(const std::string& methodName, const nlohmann::json& parameters) override
    {
        auto result = Firebolt::Transport::GetGatewayInstance().request(methodName, parameters).get();
        if (!result)
        {
            return Result<nlohmann::json>{result.error()};
        }
        return Result<nlohmann::json>{*result};
    }

    Result<SubscriptionId> subscribe(void* owner, const std::string& eventName, std::any&& notification,
                                     void (*callback)(void*, const nlohmann::json&)) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t newId = currentId_++;
        subscriptions_[newId] = SubscriptionData{owner, eventName, std::move(notification)};
        void* notificationPtr = reinterpret_cast<void*>(&subscriptions_[newId]);

        Error status = Firebolt::Transport::GetGatewayInstance().subscribe(eventName, callback, notificationPtr);

        if (Error::None != status)
        {
            subscriptions_.erase(newId);
            return Result<SubscriptionId>{status};
        }
        return Result<SubscriptionId>{newId};
    }

    std::mutex mutex_;
    std::map<uint64_t, SubscriptionData> subscriptions_;
    uint64_t currentId_{0};
};

IHelper& GetHelperInstance()
{
    static HelperImpl instance;
    return instance;
}

} // namespace Firebolt::Helpers

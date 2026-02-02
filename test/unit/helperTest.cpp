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
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Firebolt;
using namespace Firebolt::Helpers;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class MockHelper : public IHelper
{
public:
    MOCK_METHOD(Result<void>, set, (const std::string& methodName, const nlohmann::json& parameters), (override));
    MOCK_METHOD(Result<void>, invoke, (const std::string& methodName, const nlohmann::json& parameters), (override));
    MOCK_METHOD(Result<nlohmann::json>, getJson, (const std::string& methodName, const nlohmann::json& parameters),
                (override));
    MOCK_METHOD(Result<SubscriptionId>, subscribe,
                (void* owner, const std::string& eventName, std::any&& notification,
                 void (*callback)(void*, const nlohmann::json&)),
                (override));
    MOCK_METHOD(Result<void>, unsubscribe, (SubscriptionId id), (override));
    MOCK_METHOD(void, unsubscribeAll, (void* owner), (override));
};

class SubscriptionManagerTest : public ::testing::Test
{
protected:
    MockHelper mockHelper;
    void* owner = this;
    std::unique_ptr<SubscriptionManager> subscriptionManager;

    void SetUp() override { subscriptionManager = std::make_unique<SubscriptionManager>(mockHelper, owner); }
};

struct TestJson
{
    int v;
    void fromJson(const nlohmann::json& json) { v = json.at("value").get<int>(); }
    auto value() const { return v; }
};

TEST_F(SubscriptionManagerTest, Subscribe)
{
    std::function<void(int)> notification = [](int) {};
    EXPECT_CALL(mockHelper, subscribe(owner, "test.event", _, _)).WillOnce(Return(Result<SubscriptionId>{123}));

    auto result = subscriptionManager->subscribe<TestJson, int>("test.event", std::move(notification));
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 123);
}

TEST_F(SubscriptionManagerTest, Unsubscribe)
{
    EXPECT_CALL(mockHelper, unsubscribe(123)).WillOnce(Return(Result<void>(Error::None)));
    auto result = subscriptionManager->unsubscribe(123);
    EXPECT_TRUE(result);
}

TEST_F(SubscriptionManagerTest, UnsubscribeAll)
{
    EXPECT_CALL(mockHelper, unsubscribeAll(owner));
    subscriptionManager->unsubscribeAll();
    subscriptionManager.release();
}

TEST(OnPropertyChangedCallbackTest, Basic)
{
    SubscriptionData subData;
    subData.owner = nullptr;
    subData.eventName = "test.event";

    std::promise<int> promise;
    auto future = promise.get_future();
    std::function<void(int)> notification = [&promise](int val) { promise.set_value(val); };
    subData.notification = notification;

    nlohmann::json jsonResponse = {{"value", 42}};

    onPropertyChangedCallback<TestJson, int>(&subData, jsonResponse);

    auto status = future.wait_for(std::chrono::seconds(1));
    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(future.get(), 42);
}

TEST(OnPropertyChangedCallbackTest, InvalidJson)
{
    SubscriptionData subData;
    subData.owner = nullptr;
    subData.eventName = "test.event";

    std::function<void(int)> notification = [](int) { FAIL() << "Notification should not be called"; };
    subData.notification = notification;

    nlohmann::json jsonResponse = {{"wrong_key", 42}};

    onPropertyChangedCallback<TestJson, int>(&subData, jsonResponse);
}

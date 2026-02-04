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

#include "firebolt/gateway.h"
#include "firebolt/helpers.h"
#include "helpers_impl.h"
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Firebolt;
using namespace Firebolt::Helpers;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class MockGateway : public Transport::IGateway
{
public:
    MOCK_METHOD(Error, connect, (const Config&, Firebolt::Transport::ConnectionChangeCallback), (override));
    MOCK_METHOD(Error, disconnect, (), (override));
    MOCK_METHOD(std::future<Result<nlohmann::json>>, request, (const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(Error, send, (const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(Error, subscribe, (const std::string&, Firebolt::Transport::EventCallback, void*), (override));
    MOCK_METHOD(Error, unsubscribe, (const std::string&, void*), (override));
};

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

struct TestJson
{
    int v;
    void fromJson(const nlohmann::json& json) { v = json.at("value").get<int>(); }
    auto value() const { return v; }
};

class HelperTest : public ::testing::Test
{
protected:
    MockGateway mockGateway;
    HelperImpl helper{mockGateway};
};

TEST_F(HelperTest, SetSuccess)
{
    const std::string methodName = "test.set";
    const nlohmann::json params = {{"key", "value"}};

    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{nlohmann::json{}});

    EXPECT_CALL(mockGateway, request(methodName, params)).WillOnce(Return(promise.get_future()));

    auto result = helper.set(methodName, params);
    EXPECT_TRUE(result);
}

TEST_F(HelperTest, SetFailure)
{
    const std::string methodName = "test.set";
    const nlohmann::json params = {{"key", "value"}};

    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{Error::General});

    EXPECT_CALL(mockGateway, request(methodName, params)).WillOnce(Return(promise.get_future()));

    auto result = helper.set(methodName, params);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Error::General);
}

TEST_F(HelperTest, InvokeSuccess)
{
    const std::string methodName = "test.invoke";
    const nlohmann::json params = {{"key", "value"}};

    EXPECT_CALL(mockGateway, send(methodName, params)).WillOnce(Return(Error::None));

    auto result = helper.invoke(methodName, params);
    EXPECT_TRUE(result);
}

TEST_F(HelperTest, GetSuccess)
{
    const std::string methodName = "test.get";
    const nlohmann::json responseJson = {{"value", 123}};
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{responseJson});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(promise.get_future()));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 123);
}

TEST_F(HelperTest, GetJsonFailure)
{
    const std::string methodName = "test.get";
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{Error::MethodNotFound});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(promise.get_future()));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::MethodNotFound);
}

TEST_F(HelperTest, GetParseFailure)
{
    const std::string methodName = "test.get";
    const nlohmann::json invalidResponseJson = {{"wrong_key", 123}};
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{invalidResponseJson});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(promise.get_future()));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::InvalidParams);
}

class SubscriptionManagerTest : public ::testing::Test
{
protected:
    MockHelper mockHelper;
    void* owner = this;
    std::unique_ptr<SubscriptionManager> subscriptionManager;

    void SetUp() override { subscriptionManager = std::make_unique<SubscriptionManager>(mockHelper, owner); }
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

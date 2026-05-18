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
using ::testing::ByMove;
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

class HelperUTest : public ::testing::Test
{
protected:
    MockGateway mockGateway;
    HelperImpl helper{mockGateway};
};

TEST_F(HelperUTest, SetSuccess)
{
    const std::string methodName = "test.set";
    const nlohmann::json params = {{"key", "value"}};

    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{nlohmann::json{}});

    EXPECT_CALL(mockGateway, request(methodName, params)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.set(methodName, params);
    EXPECT_TRUE(result);
}

TEST_F(HelperUTest, SetFailure)
{
    const std::string methodName = "test.set";
    const nlohmann::json params = {{"key", "value"}};

    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{Error::General});

    EXPECT_CALL(mockGateway, request(methodName, params)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.set(methodName, params);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Error::General);
}

TEST_F(HelperUTest, InvokeSuccess)
{
    const std::string methodName = "test.invoke";
    const nlohmann::json params = {{"key", "value"}};

    EXPECT_CALL(mockGateway, send(methodName, params)).WillOnce(Return(Error::None));

    auto result = helper.invoke(methodName, params);
    EXPECT_TRUE(result);
}

TEST_F(HelperUTest, GetSuccess)
{
    const std::string methodName = "test.get";
    const nlohmann::json responseJson = {{"value", 123}};
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{responseJson});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 123);
}

TEST_F(HelperUTest, GetJsonFailure)
{
    const std::string methodName = "test.get";
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{Error::MethodNotFound});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::MethodNotFound);
}

TEST_F(HelperUTest, GetParseFailure)
{
    const std::string methodName = "test.get";
    const nlohmann::json invalidResponseJson = {{"wrong_key", 123}};
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{invalidResponseJson});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::InvalidParams);
}

class SubscriptionManagerUTest : public ::testing::Test
{
protected:
    MockHelper mockHelper;
    void* owner = this;
    std::unique_ptr<SubscriptionManager> subscriptionManager;

    void SetUp() override { subscriptionManager = std::make_unique<SubscriptionManager>(mockHelper, owner); }
};

TEST_F(SubscriptionManagerUTest, Subscribe)
{
    std::function<void(int)> notification = [](int) {};
    EXPECT_CALL(mockHelper, subscribe(owner, "test.event", _, _)).WillOnce(Return(Result<SubscriptionId>{123}));

    auto result = subscriptionManager->subscribe<TestJson, int>("test.event", std::move(notification));
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 123);
}

TEST_F(SubscriptionManagerUTest, Unsubscribe)
{
    EXPECT_CALL(mockHelper, unsubscribe(123)).WillOnce(Return(Result<void>(Error::None)));
    auto result = subscriptionManager->unsubscribe(123);
    EXPECT_TRUE(result);
}

TEST_F(SubscriptionManagerUTest, UnsubscribeAll)
{
    EXPECT_CALL(mockHelper, unsubscribeAll(owner));
    subscriptionManager->unsubscribeAll();
    subscriptionManager.release();
}

TEST(OnPropertyChangedCallbackUTest, Basic)
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

TEST(OnPropertyChangedCallbackUTest, InvalidJson)
{
    SubscriptionData subData;
    subData.owner = nullptr;
    subData.eventName = "test.event";

    std::function<void(int)> notification = [](int) { FAIL() << "Notification should not be called"; };
    subData.notification = notification;

    nlohmann::json jsonResponse = {{"wrong_key", 42}};

    onPropertyChangedCallback<TestJson, int>(&subData, jsonResponse);
}

// ---------------------------------------------------------------------------
// Additional helper tests for coverage gaps
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: HelperUTest.SetWithNonObjectParams
// Covers: src/helpers_impl.h:49-54 (non-object params wrapped in "value")
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, SetWithNonObjectParams)
{
    const std::string methodName = "test.set";
    const nlohmann::json params = 42; // scalar, not object

    // The helper wraps non-object params as {"value": params}
    nlohmann::json expectedParams;
    expectedParams["value"] = 42;

    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{nlohmann::json{}});

    EXPECT_CALL(mockGateway, request(methodName, expectedParams)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.set(methodName, params);
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.SetWithArrayParams
// Covers: src/helpers_impl.h:49-54 (array is not object → wrap in "value")
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, SetWithArrayParams)
{
    const std::string methodName = "test.set";
    const nlohmann::json params = nlohmann::json::array({1, 2, 3});

    nlohmann::json expectedParams;
    expectedParams["value"] = params;

    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{nlohmann::json{}});

    EXPECT_CALL(mockGateway, request(methodName, expectedParams)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.set(methodName, params);
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.InvokeFailure
// Covers: src/helpers_impl.h:63-65 (invoke returns gateway error)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, InvokeFailure)
{
    const std::string methodName = "test.invoke";
    const nlohmann::json params = {{"key", "value"}};

    EXPECT_CALL(mockGateway, send(methodName, params)).WillOnce(Return(Error::NotConnected));

    auto result = helper.invoke(methodName, params);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Error::NotConnected);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.GetWithParameters
// Covers: src/helpers_impl.h:103 (getJson called with explicit parameters)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, GetWithParameters)
{
    const std::string methodName = "test.get";
    const nlohmann::json params = {{"param1", "abc"}};
    const nlohmann::json responseJson = {{"value", 99}};
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{responseJson});

    EXPECT_CALL(mockGateway, request(methodName, params)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.get<TestJson, int>(methodName, params);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, 99);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.GetWithErrorInfo
// Covers: src/helpers_impl.h:105-106 (error propagation with errorInfo)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, GetWithErrorInfo)
{
    const std::string methodName = "test.get";
    Firebolt::ErrorInfo errorInfo(-32601, "Method not found");
    std::promise<Result<nlohmann::json>> promise;
    promise.set_value(Result<nlohmann::json>{Error::MethodNotFound, errorInfo});

    EXPECT_CALL(mockGateway, request(methodName, _)).WillOnce(Return(ByMove(promise.get_future())));

    auto result = helper.get<TestJson, int>(methodName);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::MethodNotFound);
    EXPECT_EQ(result.errorInfo().error(), -32601);
    EXPECT_EQ(result.errorInfo().message(), "Method not found");
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.SubscribeSuccess
// Covers: src/helpers_impl.h:119-133 (subscribe success flow)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, SubscribeSuccess)
{
    std::function<void(int)> notification = [](int) {};

    EXPECT_CALL(mockGateway, subscribe("test.onEvent", _, _)).WillOnce(Return(Error::None));

    IHelper& ihelper = helper;
    auto result =
        ihelper.subscribe(this, "test.onEvent", std::move(notification), onPropertyChangedCallback<TestJson, int>);
    ASSERT_TRUE(result);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.SubscribeGatewayError
// Covers: src/helpers_impl.h:127-131 (subscribe gateway error → erase + return)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, SubscribeGatewayError)
{
    std::function<void(int)> notification = [](int) {};

    EXPECT_CALL(mockGateway, subscribe("test.onEvent", _, _)).WillOnce(Return(Error::NotConnected));

    IHelper& ihelper = helper;
    auto result =
        ihelper.subscribe(this, "test.onEvent", std::move(notification), onPropertyChangedCallback<TestJson, int>);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::NotConnected);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.UnsubscribeNotFound
// Covers: src/helpers_impl.h:71-74 (unsubscribe with invalid id)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, UnsubscribeNotFound)
{
    auto result = helper.unsubscribe(9999); // non-existent id
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Error::General);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.UnsubscribeSuccess
// Covers: src/helpers_impl.h:75-79 (unsubscribe with valid id)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, UnsubscribeSuccess)
{
    // First subscribe to get a valid ID
    std::function<void(int)> notification = [](int) {};
    EXPECT_CALL(mockGateway, subscribe("test.onEvent", _, _)).WillOnce(Return(Error::None));

    IHelper& ihelper = helper;
    auto subResult =
        ihelper.subscribe(this, "test.onEvent", std::move(notification), onPropertyChangedCallback<TestJson, int>);
    ASSERT_TRUE(subResult);
    SubscriptionId id = *subResult;

    EXPECT_CALL(mockGateway, unsubscribe("test.onEvent", _)).WillOnce(Return(Error::None));

    auto result = helper.unsubscribe(id);
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.UnsubscribeAllWithOwner
// Covers: src/helpers_impl.h:83-97 (unsubscribeAll matching owner)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, UnsubscribeAllWithOwner)
{
    void* owner1 = reinterpret_cast<void*>(0x1);
    void* owner2 = reinterpret_cast<void*>(0x2);

    std::function<void(int)> notification1 = [](int) {};
    std::function<void(int)> notification2 = [](int) {};

    EXPECT_CALL(mockGateway, subscribe("event1", _, _)).WillOnce(Return(Error::None));
    EXPECT_CALL(mockGateway, subscribe("event2", _, _)).WillOnce(Return(Error::None));

    IHelper& ihelper = helper;
    ihelper.subscribe(owner1, "event1", std::move(notification1), onPropertyChangedCallback<TestJson, int>);
    ihelper.subscribe(owner2, "event2", std::move(notification2), onPropertyChangedCallback<TestJson, int>);

    // Unsubscribe all for owner1 — only event1 should be unsubscribed
    EXPECT_CALL(mockGateway, unsubscribe("event1", _)).WillOnce(Return(Error::None));
    // event2 (owner2) is cleaned up by the HelperImpl destructor
    EXPECT_CALL(mockGateway, unsubscribe("event2", _)).WillOnce(Return(Error::None));

    helper.unsubscribeAll(owner1);
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.UnsubscribeAllNoMatch
// Covers: src/helpers_impl.h:83-97 (unsubscribeAll with no matching owner)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, UnsubscribeAllNoMatch)
{
    void* owner = reinterpret_cast<void*>(0x1);
    void* otherOwner = reinterpret_cast<void*>(0x99);

    std::function<void(int)> notification = [](int) {};
    EXPECT_CALL(mockGateway, subscribe("event", _, _)).WillOnce(Return(Error::None));

    IHelper& ihelper = helper;
    ihelper.subscribe(owner, "event", std::move(notification), onPropertyChangedCallback<TestJson, int>);

    // Unsubscribe all for a different owner — no immediate calls to gateway.unsubscribe.
    // The original subscription remains active and is cleaned up by the HelperImpl destructor.
    EXPECT_CALL(mockGateway, unsubscribe("event", _)).WillOnce(Return(Error::None));
    helper.unsubscribeAll(otherOwner);
}

// ---------------------------------------------------------------------------
// Multi-arg callback test type for tuple apply path
// ---------------------------------------------------------------------------
struct TestMultiArgJson
{
    std::tuple<int, std::string> vals;
    void fromJson(const nlohmann::json& json)
    {
        vals = std::make_tuple(json.at("num").get<int>(), json.at("str").get<std::string>());
    }
    const auto& value() const { return vals; }
};

// ---------------------------------------------------------------------------
// Test name: OnPropertyChangedCallbackUTest.MultiArgCallback
// Covers: src/helpers.h:51-52 (sizeof...(Args) > 1 → std::apply branch)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST(OnPropertyChangedCallbackUTest, MultiArgCallback)
{
    SubscriptionData subData;
    subData.owner = nullptr;
    subData.eventName = "test.multiEvent";

    std::promise<std::pair<int, std::string>> promise;
    auto future = promise.get_future();
    std::function<void(int, std::string)> notification = [&promise](int n, std::string s) { promise.set_value({n, s}); };
    subData.notification = notification;

    nlohmann::json jsonResponse = {{"num", 7}, {"str", "hello"}};

    onPropertyChangedCallback<TestMultiArgJson, int, std::string>(&subData, jsonResponse);

    auto status = future.wait_for(std::chrono::seconds(1));
    ASSERT_EQ(status, std::future_status::ready);
    auto [num, str] = future.get();
    EXPECT_EQ(num, 7);
    EXPECT_EQ(str, "hello");
}

// ---------------------------------------------------------------------------
// Test name: OnPropertyChangedCallbackUTest.MultiArgInvalidJson
// Covers: helpers.h:59 (catch path for multi-arg template instantiation)
// Scenario: The multi-arg instantiation of onPropertyChangedCallback receives
//           malformed JSON that fails fromJson(). Exercises the catch branch
//           for the <TestMultiArgJson, int, std::string> instantiation.
// ---------------------------------------------------------------------------
TEST(OnPropertyChangedCallbackUTest, MultiArgInvalidJson)
{
    SubscriptionData subData;
    subData.owner = nullptr;
    subData.eventName = "test.multiEvent";

    std::function<void(int, std::string)> notification = [](int, std::string)
    { FAIL() << "Notification should not be called"; };
    subData.notification = notification;

    // Missing "num" and "str" keys that TestMultiArgJson expects
    nlohmann::json jsonResponse = {{"wrong_key", 42}};

    // Should not throw — the catch block inside onPropertyChangedCallback logs and returns
    onPropertyChangedCallback<TestMultiArgJson, int, std::string>(&subData, jsonResponse);
    // If we get here without crashing, the catch block handled the exception
}

// ---------------------------------------------------------------------------
// Test name: SubscriptionManagerUTest.SubscribeFailure
// Covers: helpers_impl.cpp:38 (SubscriptionManager propagates error)
// Scenario type: failure
// ---------------------------------------------------------------------------
TEST_F(SubscriptionManagerUTest, SubscribeFailure)
{
    std::function<void(int)> notification = [](int) {};
    EXPECT_CALL(mockHelper, subscribe(owner, "test.event", _, _))
        .WillOnce(Return(Result<SubscriptionId>{Error::NotConnected}));

    auto result = subscriptionManager->subscribe<TestJson, int>("test.event", std::move(notification));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Error::NotConnected);
}

// ---------------------------------------------------------------------------
// Test name: SubscriptionManagerUTest.DestructorCallsUnsubscribeAll
// Covers: helpers_impl.cpp:34 (destructor calls unsubscribeAll)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST_F(SubscriptionManagerUTest, DestructorCallsUnsubscribeAll)
{
    EXPECT_CALL(mockHelper, unsubscribeAll(owner)).Times(1);
    // Destroy the subscription manager — destructor should call unsubscribeAll
    subscriptionManager.reset();
}

// ---------------------------------------------------------------------------
// Branch-coverage tests
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Test name: HelperUTest.DestructorCleansUpActiveSubscriptions
// Covers: helpers_impl.h:35-42 (destructor loop over subscriptions_)
//         Also covers the false branch (empty subscriptions_ → skip loop)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, DestructorCleansUpActiveSubscriptions)
{
    // Create a HelperImpl with active subscriptions that are not
    // manually unsubscribed — the destructor should clean them up.
    auto* localHelper = new HelperImpl(mockGateway);
    IHelper* ihelper = localHelper;

    std::function<void(int)> n1 = [](int) {};
    std::function<void(int)> n2 = [](int) {};

    EXPECT_CALL(mockGateway, subscribe("evt1", _, _)).WillOnce(Return(Error::None));
    EXPECT_CALL(mockGateway, subscribe("evt2", _, _)).WillOnce(Return(Error::None));

    ihelper->subscribe(this, "evt1", std::move(n1), onPropertyChangedCallback<TestJson, int>);
    ihelper->subscribe(this, "evt2", std::move(n2), onPropertyChangedCallback<TestJson, int>);

    // Destructor should call gateway.unsubscribe for each active subscription
    EXPECT_CALL(mockGateway, unsubscribe("evt1", _)).WillOnce(Return(Error::None));
    EXPECT_CALL(mockGateway, unsubscribe("evt2", _)).WillOnce(Return(Error::None));

    delete localHelper;
}

// ---------------------------------------------------------------------------
// Test name: HelperUTest.DestructorWithNoSubscriptions
// Covers: helpers_impl.h:35-42 (destructor with empty subscriptions_ → skip)
// Scenario type: edge case
// ---------------------------------------------------------------------------
TEST_F(HelperUTest, DestructorWithNoSubscriptions)
{
    // Destructor on a HelperImpl with no subscriptions should not call unsubscribe
    auto* localHelper = new HelperImpl(mockGateway);
    // No EXPECT_CALL for unsubscribe — none should be called
    delete localHelper;
}

// ---------------------------------------------------------------------------
// Test name: GetHelperInstanceTest.ReturnsSingleton
// Covers: helpers_impl.cpp:45,47-48 (GetHelperInstance singleton factory)
// Scenario type: success
// ---------------------------------------------------------------------------
TEST(GetHelperInstanceTest, ReturnsSingleton)
{
    // GetHelperInstance() creates a static HelperImpl backed by the real gateway singleton.
    IHelper& helper1 = GetHelperInstance();
    IHelper& helper2 = GetHelperInstance();
    // Same singleton reference
    EXPECT_EQ(&helper1, &helper2);
}

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
#include "gateway_testing.h"
#include "unit/scriptedGateway.h"
#include "unit/testClock.h"
#include <algorithm>
#include <condition_variable>
#include <csignal>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>

using namespace Firebolt::Transport;
using Firebolt::Transport::Testing::ScriptedGateway;

namespace
{
constexpr auto kReadyTimeout = std::chrono::seconds(2);
}

// ---------------------------------------------------------------------------
// Test name: NotificationWorkerDeathTest.CallbackCanSubscribeWhileStopping
// Covers: Server::stopNotificationWorker(), ensureNotificationWorkerStarted()
// Scenario type: edge case — reentrant subscribe during join, then restart
// ---------------------------------------------------------------------------
TEST(NotificationWorkerDeathTest, CallbackCanSubscribeWhileStopping)
{
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
    ASSERT_EXIT(
        {
            ::alarm(10);
            ScriptedGateway server;
            if (!server.start())
                ::_exit(1);
            auto& gateway = GetGatewayInstance();
            Firebolt::Config cfg;
            cfg.wsUrl = server.url();
            std::promise<void> connected;
            auto connectedFuture = connected.get_future();
            if (gateway.connect(cfg,
                                [&connected](bool isConnected, const Firebolt::Error&)
                                {
                                    if (isConnected)
                                        connected.set_value();
                                }) != Firebolt::Error::None ||
                connectedFuture.wait_for(kReadyTimeout) != std::future_status::ready)
                ::_exit(2);

            int firstTag = 0;
            int secondTag = 0;
            std::promise<void> entered;
            auto enteredFuture = entered.get_future();
            std::promise<void> release;
            auto releaseFuture = release.get_future();
            std::promise<Firebolt::Error> subscribed;
            auto subscribedFuture = subscribed.get_future();
            std::promise<void> restarted;
            auto restartedFuture = restarted.get_future();
            auto onRestart = [&restarted](void*, const nlohmann::json&) { restarted.set_value(); };
            if (gateway.subscribe(
                    "test.worker",
                    [&](void*, const nlohmann::json&)
                    {
                        entered.set_value();
                        releaseFuture.wait();
                        subscribed.set_value(gateway.subscribe("test.worker", onRestart, &secondTag));
                    },
                    &firstTag) != Firebolt::Error::None)
                ::_exit(3);
            server.notify("test.worker", nlohmann::json::object());
            if (enteredFuture.wait_for(kReadyTimeout) != std::future_status::ready)
                ::_exit(4);

            auto stopped = std::async(std::launch::async, testStopGatewayNotificationWorker);
            const auto deadline = std::chrono::steady_clock::now() + kReadyTimeout;
            while (!testGatewayNotificationWorkerStopRequested())
            {
                if (std::chrono::steady_clock::now() >= deadline)
                    ::_exit(5);
                std::this_thread::yield();
            }
            release.set_value();
            if (subscribedFuture.wait_for(kReadyTimeout) != std::future_status::ready)
                ::_exit(6);
            if (subscribedFuture.get() != Firebolt::Error::None)
                ::_exit(7);
            if (stopped.wait_for(kReadyTimeout) != std::future_status::ready)
                ::_exit(8);
            stopped.get();
            if (!testGatewayNotificationWorkerStopRequested())
                ::_exit(9);

            // A new subscribe after the join must restart delivery, not leave the stop flag latched.
            gateway.unsubscribe("test.worker", &firstTag);
            int thirdTag = 0;
            if (gateway.subscribe("test.restart", [](void*, const nlohmann::json&) {}, &thirdTag) != Firebolt::Error::None)
                ::_exit(10);
            server.notify("test.worker", nlohmann::json::object());
            if (restartedFuture.wait_for(kReadyTimeout) != std::future_status::ready)
                ::_exit(11);
            gateway.disconnect();
            server.stop();
            ::_exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

class HibernationUTest : public ::testing::Test
{
protected:
    // Clocks outlive TearDown(): the gateway's watchdog may sample them until disconnect().
    Testing::JumpClock jumpClock_;
    Testing::ManualClock frozenClock_;
    ScriptedGateway server_;
    std::atomic<unsigned> frozenClockSamples_{0};
    std::mutex frozenClockSamplesMutex_;
    std::condition_variable frozenClockSamplesCv_;

    void SetUp() override { ASSERT_TRUE(server_.start()) << "ScriptedGateway failed to start"; }

    void TearDown() override
    {
        GetGatewayInstance().setResumeListener(nullptr);
        GetGatewayInstance().disconnect();
        testSetGatewayTimeSource(nullptr);
        server_.stop();
    }

    Firebolt::Config makeConfig(unsigned waitTime_ms = 1000, unsigned resumeGrace_ms = 0) const
    {
        Firebolt::Config cfg;
        cfg.wsUrl = server_.url();
        cfg.waitTime_ms = waitTime_ms;
        cfg.resumeGrace_ms = resumeGrace_ms;
        cfg.log.level = Firebolt::LogLevel::Debug;
        return cfg;
    }

    void setObservedFrozenClock()
    {
        frozenClockSamples_.store(0);
        auto source = frozenClock_.source();
        testSetGatewayTimeSource(
            [this, source = std::move(source)]() mutable
            {
                const auto now = source();
                frozenClockSamples_.fetch_add(1);
                frozenClockSamplesCv_.notify_all();
                return now;
            });
    }

    bool waitForAckWaitToStart()
    {
        std::unique_lock<std::mutex> lock(frozenClockSamplesMutex_);
        return frozenClockSamplesCv_.wait_for(lock, kReadyTimeout, [this] { return frozenClockSamples_.load() >= 3; });
    }

    bool waitForFrozenClockSampleAfter(unsigned previousSampleCount)
    {
        std::unique_lock<std::mutex> lock(frozenClockSamplesMutex_);
        return frozenClockSamplesCv_.wait_for(lock, kReadyTimeout, [this, previousSampleCount]
                                              { return frozenClockSamples_.load() > previousSampleCount; });
    }

    void requireAckWaitStarted(std::future<Firebolt::Error>& pending, const std::string& event)
    {
        if (waitForAckWaitToStart())
        {
            return;
        }
        server_.release(event);
        EXPECT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
        FAIL() << "ACK wait did not reach its start sample";
    }

    // Advances the jump clock in sub-threshold steps, sampling after each, so all of it counts as active time.
    void runActive(std::chrono::milliseconds total)
    {
        constexpr std::chrono::milliseconds kStep{1000};
        while (total > std::chrono::milliseconds(0))
        {
            const std::chrono::milliseconds step = std::min(total, kStep);
            jumpClock_.jump(step);
            testRunGatewayWatchdogTick();
            total -= step;
        }
    }

    void stopFor(std::chrono::milliseconds duration)
    {
        jumpClock_.jump(duration);
        testRunGatewayWatchdogTick();
    }

    IGateway& connectAndWait(const Firebolt::Config& cfg)
    {
        IGateway& gateway = GetGatewayInstance();
        auto connected = std::make_shared<std::promise<void>>();
        auto connectedFuture = connected->get_future();
        Firebolt::Error err = gateway.connect(cfg,
                                              [connected](bool isConnected, const Firebolt::Error& /*err*/)
                                              {
                                                  if (isConnected)
                                                  {
                                                      try
                                                      {
                                                          connected->set_value();
                                                      }
                                                      catch (const std::future_error&)
                                                      {
                                                      }
                                                  }
                                              });
        EXPECT_EQ(err, Firebolt::Error::None);
        EXPECT_EQ(connectedFuture.wait_for(kReadyTimeout), std::future_status::ready);
        return gateway;
    }
};

// ---------------------------------------------------------------------------
// ScriptedGateway self-test: a request whose method is not held is answered
// immediately with an empty result object.
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, ScriptedGatewayAnswersUnheldRequest)
{
    IGateway& gateway = connectAndWait(makeConfig());

    auto future = gateway.request("test.free", nlohmann::json::object());

    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// ScriptedGateway self-test: a held method is observed by the server but not
// answered until release() is called; then the response reaches the caller.
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, ScriptedGatewayHoldsUntilReleased)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig());

    auto future = gateway.request("test.held", nlohmann::json::object());

    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    server_.release("test.held");

    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    EXPECT_TRUE(result);
}

// ---------------------------------------------------------------------------
// ScriptedGateway self-test: notify() pushes an event to a subscribed client.
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, ScriptedGatewayDeliversNotification)
{
    IGateway& gateway = connectAndWait(makeConfig());
    std::promise<nlohmann::json> received;
    auto receivedFuture = received.get_future();

    Firebolt::Error err = gateway.subscribe(
        "test.onEvent",
        [&received](void* /*usercb*/, const nlohmann::json& params)
        {
            try
            {
                received.set_value(params);
            }
            catch (const std::future_error&)
            {
            }
        },
        this);
    ASSERT_EQ(err, Firebolt::Error::None);

    server_.notify("test.onEvent", nlohmann::json{{"fired", true}});

    ASSERT_EQ(receivedFuture.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_TRUE(receivedFuture.get()["fired"].get<bool>());
    EXPECT_EQ(gateway.unsubscribe("test.onEvent", this), Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: HibernationConfigUTest.StopGapThresholdDefault
// Covers: Firebolt::Config::stopGapThreshold_ms
// Scenario type: success
// Spec: hibernation-resilience § External Interfaces
// ---------------------------------------------------------------------------
TEST(HibernationConfigUTest, StopGapThresholdDefault)
{
    Firebolt::Config cfg;
    EXPECT_EQ(cfg.stopGapThreshold_ms, 2000u);
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.ConnectAppliesConfiguredGapThreshold
// Covers: GatewayImpl::connect() applying stopGapThreshold_ms
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Configured threshold is honored
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, ConnectAppliesConfiguredGapThreshold)
{
    Firebolt::Config cfg = makeConfig();
    cfg.watchdogCycle_ms = 500;
    cfg.stopGapThreshold_ms = 10000;
    connectAndWait(cfg);

    EXPECT_EQ(testGatewayGapThreshold(), std::chrono::milliseconds(10000));
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.ConnectClampsTooSmallGapThreshold
// Covers: GatewayImpl::connect() clamping stopGapThreshold_ms to 4 x watchdogCycle_ms
// Scenario type: edge case
// Spec: hibernation-resilience § Requirement: Gap threshold distinguishes a stop from jitter
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, ConnectClampsTooSmallGapThreshold)
{
    Firebolt::Config cfg = makeConfig();
    cfg.watchdogCycle_ms = 500;
    cfg.stopGapThreshold_ms = 100;
    connectAndWait(cfg);

    EXPECT_EQ(testGatewayGapThreshold(), std::chrono::milliseconds(2000));
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.PendingRequestSurvivesStop
// Covers: Client::checkPromises() measuring active time
// Scenario type: success
// Spec: hibernation-resilience § Requirement: Request timeouts count active time only
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, PendingRequestSurvivesStop)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(1000));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    jumpClock_.jump(std::chrono::milliseconds(10000));
    testRunGatewayWatchdogTick();
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout)
        << "request was failed during a stop";

    server_.release("test.held");
    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.NormalTimeoutStillFires
// Covers: Client::checkPromises() with active time below the gap threshold
// Scenario type: failure path
// Spec: hibernation-resilience § Requirement: Request timeouts count active time only
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, NormalTimeoutStillFires)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(1000));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    jumpClock_.jump(std::chrono::milliseconds(1100));
    testRunGatewayWatchdogTick();

    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
    server_.releaseAll();
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.TimeoutResumesCountingAfterStop
// Covers: Client::checkPromises() accumulating active time either side of a stop
// Scenario type: edge case
// Spec: hibernation-resilience § Requirement: Request timeouts count active time only
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, TimeoutResumesCountingAfterStop)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(1000));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    jumpClock_.jump(std::chrono::milliseconds(600));
    testRunGatewayWatchdogTick();
    jumpClock_.jump(std::chrono::milliseconds(10000));
    testRunGatewayWatchdogTick();
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    jumpClock_.jump(std::chrono::milliseconds(500));
    testRunGatewayWatchdogTick();

    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
    server_.releaseAll();
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.SubscribeAckHeldAcrossStopSucceeds
// Covers: GatewayImpl::subscribe() acknowledgement wait measured in active time
// Scenario type: success
// Spec: hibernation-resilience § Requirement: Subscription acknowledgement waits count active time only
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, SubscribeAckHeldAcrossStopSucceeds)
{
    server_.holdMethod("test.onEvent");
    IGateway& gateway = connectAndWait(makeConfig());
    testSetGatewayTimeSource(frozenClock_.source());

    auto pending = std::async(std::launch::async, [&gateway, this]()
                              { return gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this); });
    ASSERT_TRUE(server_.waitForRequest("test.onEvent", 1, kReadyTimeout));

    frozenClock_.advance(std::chrono::milliseconds(10000));
    EXPECT_EQ(pending.wait_for(std::chrono::milliseconds(150)), std::future_status::timeout)
        << "subscribe gave up although no active time elapsed";

    server_.release("test.onEvent");
    ASSERT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(pending.get(), Firebolt::Error::None);
    EXPECT_EQ(gateway.unsubscribe("test.onEvent", this), Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.SubscribeMissingAckTimesOut
// Covers: GatewayImpl::subscribe() failure path and local rollback
// Scenario type: failure path
// Spec: hibernation-resilience § Requirement: Subscription acknowledgement waits count active time only
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, SubscribeMissingAckTimesOut)
{
    server_.holdMethod("test.onEvent");
    Firebolt::Config cfg = makeConfig(60);
    cfg.watchdogCycle_ms = 60000;
    IGateway& gateway = connectAndWait(cfg);
    setObservedFrozenClock();

    auto pending = std::async(std::launch::async, [&gateway, this]()
                              { return gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this); });
    ASSERT_TRUE(server_.waitForRequest("test.onEvent", 1, kReadyTimeout));
    requireAckWaitStarted(pending, "test.onEvent");

    frozenClock_.advance(std::chrono::milliseconds(61));
    ASSERT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(pending.get(), Firebolt::Error::Timedout);

    // The local subscription was rolled back, so the same subscriber can subscribe again.
    server_.release("test.onEvent");
    EXPECT_EQ(gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this), Firebolt::Error::None);
    EXPECT_EQ(gateway.unsubscribe("test.onEvent", this), Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.UnsubscribeAckHeldAcrossStopSucceeds
// Covers: GatewayImpl::unsubscribe() acknowledgement wait measured in active time
// Scenario type: success
// Spec: hibernation-resilience § Requirement: Subscription acknowledgement waits count active time only
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, UnsubscribeAckHeldAcrossStopSucceeds)
{
    IGateway& gateway = connectAndWait(makeConfig());
    ASSERT_EQ(gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this), Firebolt::Error::None);
    server_.holdMethod("test.onEvent");
    testSetGatewayTimeSource(frozenClock_.source());

    auto pending =
        std::async(std::launch::async, [&gateway, this]() { return gateway.unsubscribe("test.onEvent", this); });
    ASSERT_TRUE(server_.waitForRequest("test.onEvent", 2, kReadyTimeout));

    frozenClock_.advance(std::chrono::milliseconds(10000));
    EXPECT_EQ(pending.wait_for(std::chrono::milliseconds(150)), std::future_status::timeout)
        << "unsubscribe gave up although no active time elapsed";

    server_.release("test.onEvent");
    ASSERT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(pending.get(), Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.UnsubscribeMissingAckReturnsAfterActiveTimeout
// Covers: GatewayImpl::unsubscribe() missing-acknowledgement path
// Scenario type: failure path
// Spec: hibernation-resilience § Scenario: Missing unsubscribe acknowledgement
// Note: unsubscribe has always returned None here; the local removal already succeeded.
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, UnsubscribeMissingAckReturnsAfterActiveTimeout)
{
    Firebolt::Config cfg = makeConfig();
    cfg.watchdogCycle_ms = 60000;
    IGateway& gateway = connectAndWait(cfg);
    ASSERT_EQ(gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this), Firebolt::Error::None);
    server_.holdMethod("test.onEvent");
    setObservedFrozenClock();

    auto pending =
        std::async(std::launch::async, [&gateway, this]() { return gateway.unsubscribe("test.onEvent", this); });
    ASSERT_TRUE(server_.waitForRequest("test.onEvent", 2, kReadyTimeout));
    requireAckWaitStarted(pending, "test.onEvent");

    frozenClock_.advance(std::chrono::milliseconds(60));
    ASSERT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(pending.get(), Firebolt::Error::None);
    server_.releaseAll();
}

// ---------------------------------------------------------------------------
// Test name: HibernationConfigUTest.ResumeGraceDefault
// Covers: Firebolt::Config::resumeGrace_ms
// Scenario type: success
// Spec: hibernation-resilience § External Interfaces
// ---------------------------------------------------------------------------
TEST(HibernationConfigUTest, ResumeGraceDefault)
{
    Firebolt::Config cfg;
    EXPECT_EQ(cfg.resumeGrace_ms, 5000u);
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.ResponseHeldAfterThawWithinGrace
// Covers: Client::checkPromises() grace-window deadline
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Response held after thaw beyond waitTime_ms
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, ResponseHeldAfterThawWithinGrace)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(3000, 5000));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    stopFor(std::chrono::milliseconds(10000));
    runActive(std::chrono::milliseconds(4000));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout)
        << "request failed inside the grace window";

    server_.release("test.held");
    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.RequestSentJustAfterThawWithinGrace
// Covers: Client::checkPromises() grace-window deadline for requests sent after resume
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Request sent just after thaw is held
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, RequestSentJustAfterThawWithinGrace)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(3000, 5000));
    testSetGatewayTimeSource(jumpClock_.source());

    stopFor(std::chrono::milliseconds(10000));
    runActive(std::chrono::milliseconds(100));
    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    runActive(std::chrono::milliseconds(4400));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout)
        << "request failed inside the grace window";

    server_.release("test.held");
    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_TRUE(result) << "error=" << static_cast<int>(result.error());
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.GenuineFailureAfterGrace
// Covers: Client::checkPromises() grace-window expiry
// Scenario type: failure path
// Spec: hibernation-resilience § Scenario: Genuine failure is still reported after the grace period
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, GenuineFailureAfterGrace)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(3000, 5000));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    stopFor(std::chrono::milliseconds(10000));
    runActive(std::chrono::milliseconds(4500));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    runActive(std::chrono::milliseconds(1000));
    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
    server_.releaseAll();
}

TEST_F(HibernationUTest, RejectedDuplicateConnectPreservesResumeGrace)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(1000, 5000));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    stopFor(std::chrono::milliseconds(10000));
    Firebolt::Config duplicateConfig = makeConfig(100, 0);
    const Firebolt::Error duplicateStatus = gateway.connect(duplicateConfig, [](bool, const Firebolt::Error&) {});
    ASSERT_EQ(duplicateStatus, Firebolt::Error::AlreadyConnected);

    runActive(std::chrono::milliseconds(2000));
    const bool requestStillPending = future.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
    server_.release("test.held");

    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_TRUE(requestStillPending) << "rejected duplicate connect cleared the active resume grace window";
    EXPECT_TRUE(future.get());
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.GraceDisabledRestoresStrictTimeout
// Covers: Client::checkPromises() with resumeGrace_ms = 0
// Scenario type: edge case
// Spec: hibernation-resilience § Scenario: Grace disabled
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, GraceDisabledRestoresStrictTimeout)
{
    server_.holdMethod("test.held");
    IGateway& gateway = connectAndWait(makeConfig(3000, 0));
    testSetGatewayTimeSource(jumpClock_.source());

    auto future = gateway.request("test.held", nlohmann::json::object());
    ASSERT_TRUE(server_.waitForRequest("test.held", 1, kReadyTimeout));

    stopFor(std::chrono::milliseconds(10000));
    runActive(std::chrono::milliseconds(2500));
    EXPECT_EQ(future.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);

    runActive(std::chrono::milliseconds(600));
    ASSERT_EQ(future.wait_for(kReadyTimeout), std::future_status::ready);
    Firebolt::Result<nlohmann::json> result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
    server_.releaseAll();
}

// ---------------------------------------------------------------------------
// Test name: HibernationUTest.SubscribeLateAckDuringGraceSucceeds
// Covers: GatewayImpl::subscribe() acknowledgement wait extended by the grace window
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Late acknowledgement during the grace window
// ---------------------------------------------------------------------------
TEST_F(HibernationUTest, SubscribeLateAckDuringGraceSucceeds)
{
    IGateway& gateway = connectAndWait(makeConfig(3000, 5000));
    testSetGatewayTimeSource(frozenClock_.source());
    frozenClock_.advance(std::chrono::milliseconds(10000));
    testRunGatewayWatchdogTick();
    server_.holdMethod("test.onEvent");

    auto pending = std::async(std::launch::async, [&gateway, this]()
                              { return gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this); });
    ASSERT_TRUE(server_.waitForRequest("test.onEvent", 1, kReadyTimeout));

    frozenClock_.advance(std::chrono::milliseconds(1000));
    EXPECT_EQ(pending.wait_for(std::chrono::milliseconds(150)), std::future_status::timeout)
        << "subscribe gave up inside the grace window";

    server_.release("test.onEvent");
    ASSERT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(pending.get(), Firebolt::Error::None);
    EXPECT_EQ(gateway.unsubscribe("test.onEvent", this), Firebolt::Error::None);
}

TEST_F(HibernationUTest, SubscribeAckWaitUsesGraceOpenedDuringWait)
{
    server_.holdMethod("test.onEvent");
    Firebolt::Config cfg = makeConfig(3000, 5000);
    cfg.watchdogCycle_ms = 1000;
    IGateway& gateway = connectAndWait(cfg);
    setObservedFrozenClock();

    auto pending = std::async(std::launch::async, [&gateway, this]()
                              { return gateway.subscribe("test.onEvent", [](void*, const nlohmann::json&) {}, this); });
    ASSERT_TRUE(server_.waitForRequest("test.onEvent", 1, kReadyTimeout));
    requireAckWaitStarted(pending, "test.onEvent");

    unsigned sampleCount = frozenClockSamples_.load();
    frozenClock_.advance(std::chrono::milliseconds(40));
    ASSERT_TRUE(waitForFrozenClockSampleAfter(sampleCount));

    sampleCount = frozenClockSamples_.load();
    frozenClock_.advance(std::chrono::milliseconds(10000));
    ASSERT_TRUE(waitForFrozenClockSampleAfter(sampleCount));

    sampleCount = frozenClockSamples_.load();
    frozenClock_.advance(std::chrono::milliseconds(100));
    ASSERT_TRUE(waitForFrozenClockSampleAfter(sampleCount));
    EXPECT_EQ(pending.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout)
        << "subscribe timed out using its pre-resume budget instead of the active grace deadline";

    server_.release("test.onEvent");
    ASSERT_EQ(pending.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(pending.get(), Firebolt::Error::None);
    EXPECT_EQ(gateway.unsubscribe("test.onEvent", this), Firebolt::Error::None);
}

// Resume-listener tests use a long watchdog cycle so only explicit testRunGatewayWatchdogTick() calls
// sample and dispatch; the effective gap threshold is then 4 x 60000 ms.
class ResumeListenerUTest : public HibernationUTest
{
protected:
    static constexpr std::chrono::milliseconds kStop{300000};

    struct Probe
    {
        std::mutex mtx;
        int calls = 0;
        std::chrono::milliseconds total{0};
        std::thread::id thread;
        std::promise<void> firstCall;
    };
    Probe probe_;
    int barrierTag_ = 0;

    Firebolt::Config makeResumeConfig(unsigned waitTime_ms = 1000) const
    {
        Firebolt::Config cfg = makeConfig(waitTime_ms, 0);
        cfg.watchdogCycle_ms = 60000;
        return cfg;
    }

    ResumeCallback probeListener()
    {
        return [this](std::chrono::milliseconds stoppedFor)
        {
            std::lock_guard<std::mutex> lock(probe_.mtx);
            ++probe_.calls;
            probe_.total += stoppedFor;
            probe_.thread = std::this_thread::get_id();
            if (probe_.calls == 1)
            {
                probe_.firstCall.set_value();
            }
        };
    }

    int calls()
    {
        std::lock_guard<std::mutex> lock(probe_.mtx);
        return probe_.calls;
    }

    // FIFO barrier: once an event pushed now is delivered, every item queued earlier on the
    // notification worker (including resume dispatches) has run.
    void drainNotificationWorker(IGateway& gateway)
    {
        auto delivered = std::make_shared<std::promise<void>>();
        auto deliveredFuture = delivered->get_future();
        ASSERT_EQ(gateway.subscribe(
                      "test.barrier",
                      [delivered](void*, const nlohmann::json&)
                      {
                          try
                          {
                              delivered->set_value();
                          }
                          catch (const std::future_error&)
                          {
                          }
                      },
                      &barrierTag_),
                  Firebolt::Error::None);
        server_.notify("test.barrier", nlohmann::json{{"barrier", true}});
        ASSERT_EQ(deliveredFuture.wait_for(kReadyTimeout), std::future_status::ready);
        gateway.unsubscribe("test.barrier", &barrierTag_);
    }
};

// ---------------------------------------------------------------------------
// Test name: ResumeListenerUTest.CalledOnceAfterStop
// Covers: IGateway::setResumeListener(), GatewayImpl::watchdogTick() resume dispatch
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Listener notified after a stop
// ---------------------------------------------------------------------------
TEST_F(ResumeListenerUTest, CalledOnceAfterStop)
{
    IGateway& gateway = connectAndWait(makeResumeConfig());
    testSetGatewayTimeSource(jumpClock_.source());
    gateway.setResumeListener(probeListener());
    auto firstCall = probe_.firstCall.get_future();

    stopFor(kStop);

    ASSERT_EQ(firstCall.wait_for(kReadyTimeout), std::future_status::ready) << "listener not invoked";
    testRunGatewayWatchdogTick();
    drainNotificationWorker(gateway);
    std::lock_guard<std::mutex> lock(probe_.mtx);
    EXPECT_EQ(probe_.calls, 1);
    EXPECT_GE(probe_.total, kStop);
    EXPECT_NE(probe_.thread, std::this_thread::get_id()) << "listener ran on the watchdog (tick) thread";
}

// ---------------------------------------------------------------------------
// Test name: ResumeListenerUTest.NotCalledWithoutStop
// Covers: GatewayImpl::watchdogTick() with no gap
// Scenario type: edge case
// Spec: hibernation-resilience § Scenario: No notification without a stop
// ---------------------------------------------------------------------------
TEST_F(ResumeListenerUTest, NotCalledWithoutStop)
{
    IGateway& gateway = connectAndWait(makeResumeConfig());
    testSetGatewayTimeSource(jumpClock_.source());
    gateway.setResumeListener(probeListener());

    runActive(std::chrono::milliseconds(5000));

    drainNotificationWorker(gateway);
    EXPECT_EQ(calls(), 0);
}

// ---------------------------------------------------------------------------
// Test name: ResumeListenerUTest.GapsBeforeDispatchAreCoalesced
// Covers: ActiveClock::takeStoppedTime() coalescing through GatewayImpl dispatch
// Scenario type: edge case
// Spec: hibernation-resilience § Scenario: Multiple gaps before dispatch are coalesced
// ---------------------------------------------------------------------------
TEST_F(ResumeListenerUTest, GapsBeforeDispatchAreCoalesced)
{
    IGateway& gateway = connectAndWait(makeResumeConfig());
    testSetGatewayTimeSource(jumpClock_.source());
    gateway.setResumeListener(probeListener());

    // request() samples the clock on entry, so each jump below is detected as its own gap.
    jumpClock_.jump(kStop);
    auto first = gateway.request("test.free", nlohmann::json::object());
    jumpClock_.jump(kStop);
    auto second = gateway.request("test.free", nlohmann::json::object());
    testRunGatewayWatchdogTick();

    drainNotificationWorker(gateway);
    std::lock_guard<std::mutex> lock(probe_.mtx);
    EXPECT_EQ(probe_.calls, 1);
    EXPECT_GE(probe_.total, 2 * kStop);
    EXPECT_EQ(first.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(second.wait_for(kReadyTimeout), std::future_status::ready);
}

TEST_F(ResumeListenerUTest, GapsDetectedBeforeQueuedResumeDeliveryAreCoalesced)
{
    IGateway& gateway = connectAndWait(makeResumeConfig());
    testSetGatewayTimeSource(jumpClock_.source());
    gateway.setResumeListener(probeListener());
    auto firstCall = probe_.firstCall.get_future();

    std::promise<void> callbackEntered;
    auto callbackEnteredFuture = callbackEntered.get_future();
    std::promise<void> releaseCallback;
    auto releaseCallbackFuture = releaseCallback.get_future();
    bool callbackReleased = false;
    const auto releaseWorker = [&]
    {
        if (!callbackReleased)
        {
            releaseCallback.set_value();
            callbackReleased = true;
        }
    };

    int blockerTag = 0;
    ASSERT_EQ(gateway.subscribe(
                  "test.blocker",
                  [&callbackEntered, &releaseCallbackFuture](void*, const nlohmann::json&)
                  {
                      callbackEntered.set_value();
                      releaseCallbackFuture.wait();
                  },
                  &blockerTag),
              Firebolt::Error::None);
    server_.notify("test.blocker", nlohmann::json::object());
    ASSERT_EQ(callbackEnteredFuture.wait_for(kReadyTimeout), std::future_status::ready);

    stopFor(kStop);
    jumpClock_.jump(kStop);
    auto request = gateway.request("test.free", nlohmann::json::object());
    if (request.wait_for(kReadyTimeout) != std::future_status::ready)
    {
        releaseWorker();
        FAIL() << "request did not complete while the resume callback was queued";
    }
    auto requestResult = request.get();
    if (!requestResult)
    {
        releaseWorker();
        FAIL() << "request failed while the resume callback was queued";
    }

    releaseWorker();
    ASSERT_EQ(firstCall.wait_for(kReadyTimeout), std::future_status::ready);
    testRunGatewayWatchdogTick();
    drainNotificationWorker(gateway);

    {
        std::lock_guard<std::mutex> lock(probe_.mtx);
        EXPECT_EQ(probe_.calls, 1);
        EXPECT_GE(probe_.total, 2 * kStop);
    }
    EXPECT_EQ(gateway.unsubscribe("test.blocker", &blockerTag), Firebolt::Error::None);
}

// ---------------------------------------------------------------------------
// Test name: ResumeListenerUTest.ClearedListenerNotCalled
// Covers: IGateway::setResumeListener(nullptr)
// Scenario type: edge case
// Spec: hibernation-resilience § Scenario: Cleared listener is not invoked
// ---------------------------------------------------------------------------
TEST_F(ResumeListenerUTest, ClearedListenerNotCalled)
{
    IGateway& gateway = connectAndWait(makeResumeConfig());
    testSetGatewayTimeSource(jumpClock_.source());
    gateway.setResumeListener(probeListener());
    gateway.setResumeListener(nullptr);

    stopFor(kStop);

    drainNotificationWorker(gateway);
    EXPECT_EQ(calls(), 0);
}

// ---------------------------------------------------------------------------
// Test name: ResumeListenerUTest.ListenerMayBlockOnRequest
// Covers: resume dispatch on the notification worker; watchdog not stalled by the listener
// Scenario type: success
// Spec: hibernation-resilience § Scenario: Listener may issue requests
// ---------------------------------------------------------------------------
TEST_F(ResumeListenerUTest, ListenerMayBlockOnRequest)
{
    server_.holdMethod("test.fromListener");
    IGateway& gateway = connectAndWait(makeResumeConfig(1000));
    testSetGatewayTimeSource(jumpClock_.source());
    std::promise<Firebolt::Error> outcome;
    auto outcomeFuture = outcome.get_future();
    gateway.setResumeListener(
        [&gateway, &outcome](std::chrono::milliseconds /*stoppedFor*/)
        {
            auto future = gateway.request("test.fromListener", nlohmann::json::object());
            if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            {
                outcome.set_value(Firebolt::Error::General);
                return;
            }
            Firebolt::Result<nlohmann::json> result = future.get();
            outcome.set_value(result ? Firebolt::Error::None : result.error());
        });

    stopFor(kStop);
    ASSERT_TRUE(server_.waitForRequest("test.fromListener", 1, kReadyTimeout)) << "listener did not run";

    // The listener is blocked; the watchdog must still be able to time its request out.
    runActive(std::chrono::milliseconds(1100));

    ASSERT_EQ(outcomeFuture.wait_for(kReadyTimeout), std::future_status::ready);
    EXPECT_EQ(outcomeFuture.get(), Firebolt::Error::Timedout);
    server_.releaseAll();
}

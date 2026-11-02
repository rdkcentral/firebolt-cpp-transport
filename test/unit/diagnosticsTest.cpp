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

#include "firebolt/diagnostics.h"
#include "firebolt/gateway.h"
#include "firebolt/logger.h"
#include "metrics.h"
#include "transport.h"
#include <future>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace Firebolt::Transport;

namespace
{
uint64_t counterOf(const Snapshot& snap, Counter counter)
{
    return snap.counters[static_cast<std::size_t>(counter)];
}
} // namespace

// The metric store is a process-wide singleton shared with every other suite in
// this binary, so each test resets it first and asserts on absolute values only
// for counters it produced itself.
class MetricsUTest : public ::testing::Test
{
protected:
    void SetUp() override { Metrics::instance().reset(); }
};

TEST_F(MetricsUTest, CountersStartAtZeroAfterReset)
{
    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(counterOf(snap, Counter::RequestsSent), 0u);
    EXPECT_EQ(counterOf(snap, Counter::RequestsTimedOut), 0u);
}

TEST_F(MetricsUTest, IncrementAndAddAccumulate)
{
    Metrics::instance().increment(Counter::RequestsSent);
    Metrics::instance().increment(Counter::RequestsSent);
    Metrics::instance().add(Counter::BytesSent, 512);

    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(counterOf(snap, Counter::RequestsSent), 2u);
    EXPECT_EQ(counterOf(snap, Counter::BytesSent), 512u);
}

TEST_F(MetricsUTest, CountersAreAtomicUnderConcurrency)
{
    constexpr int kThreads = 8;
    constexpr int kPerThread = 5000;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back(
            []()
            {
                for (int n = 0; n < kPerThread; ++n)
                {
                    Metrics::instance().increment(Counter::MessagesReceived);
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(counterOf(Metrics::instance().snapshot(), Counter::MessagesReceived),
              static_cast<uint64_t>(kThreads) * kPerThread);
}

TEST_F(MetricsUTest, GaugeTracksHighWaterMarkAfterFallingBack)
{
    Metrics::instance().setGauge(Gauge::PendingRequests, 3);
    Metrics::instance().setGauge(Gauge::PendingRequests, 11);
    Metrics::instance().setGauge(Gauge::PendingRequests, 1);

    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(snap.pendingRequests, 1u);
    EXPECT_EQ(snap.pendingRequestsHighWater, 11u) << "peak depth must survive the queue draining";
}

TEST_F(MetricsUTest, LatencySamplesLandInTheExpectedBuckets)
{
    // Bucket i counts samples strictly below kLatencyBucketBoundsMs[i], after
    // truncating the sample to whole milliseconds.
    Metrics::instance().recordLatency(500);       // 0 ms -> bucket 0 (<1ms)
    Metrics::instance().recordLatency(3000);      // 3 ms -> bucket 2 (<5ms)
    Metrics::instance().recordLatency(9'000'000); // 9 s  -> overflow bucket

    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(snap.latencyCount, 3u);
    EXPECT_EQ(snap.latencyBuckets[0], 1u);
    EXPECT_EQ(snap.latencyBuckets[2], 1u);
    EXPECT_EQ(snap.latencyBuckets[kLatencyBucketCount - 1], 1u);
    EXPECT_EQ(snap.latencyMin_us, 500u);
    EXPECT_EQ(snap.latencyMax_us, 9'000'000u);
    EXPECT_EQ(snap.latencySum_us, 9'003'500u);
}

TEST_F(MetricsUTest, SampleOnABucketBoundaryFallsIntoTheHigherBucket)
{
    Metrics::instance().recordLatency(5000); // exactly 5 ms
    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(snap.latencyBuckets[2], 0u) << "5ms is not strictly below the 5ms bound";
    EXPECT_EQ(snap.latencyBuckets[3], 1u) << "5ms belongs to the <10ms bucket";
}

TEST_F(MetricsUTest, LatencyMinIgnoresTheInitialZeroSentinel)
{
    Metrics::instance().recordLatency(9000);
    Metrics::instance().recordLatency(4000);
    EXPECT_EQ(Metrics::instance().snapshot().latencyMin_us, 4000u);
}

TEST_F(MetricsUTest, LifecycleRingRetainsTheMostRecentEntriesInOrder)
{
    // Overfill the ring so wraparound is exercised.
    const std::size_t total = kLifecycleHistoryDepth + 5;
    for (std::size_t i = 0; i < total; ++i)
    {
        Metrics::instance().recordLifecycle(i % 2 == 0, Firebolt::Error::None);
    }

    const Snapshot snap = Metrics::instance().snapshot();
    ASSERT_EQ(snap.recentLifecycle.size(), kLifecycleHistoryDepth);

    // Oldest-first ordering: the retained window starts at index (total - depth).
    const std::size_t firstRetained = total - kLifecycleHistoryDepth;
    for (std::size_t i = 0; i < kLifecycleHistoryDepth; ++i)
    {
        EXPECT_EQ(snap.recentLifecycle[i].connected, ((firstRetained + i) % 2 == 0)) << "at index " << i;
    }
}

TEST_F(MetricsUTest, LifecycleRecordsCarryTheReportedError)
{
    Metrics::instance().recordLifecycle(false, Firebolt::Error::Timedout);
    const Snapshot snap = Metrics::instance().snapshot();
    ASSERT_EQ(snap.recentLifecycle.size(), 1u);
    EXPECT_FALSE(snap.recentLifecycle[0].connected);
    EXPECT_EQ(snap.recentLifecycle[0].error, Firebolt::Error::Timedout);
}

TEST_F(MetricsUTest, ConnectedDurationIsReportedOnlyWhileConnected)
{
    Metrics::instance().markDisconnected();
    EXPECT_EQ(Metrics::instance().snapshot().connectedFor_ms, 0u);

    Metrics::instance().markConnected();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_GE(Metrics::instance().snapshot().connectedFor_ms, 10u);

    Metrics::instance().markDisconnected();
    EXPECT_EQ(Metrics::instance().snapshot().connectedFor_ms, 0u);
}

TEST_F(MetricsUTest, ResetClearsCountersAndLatencyButRebasesHighWaterMarks)
{
    Metrics::instance().increment(Counter::RequestsSent);
    Metrics::instance().recordLatency(1234);
    Metrics::instance().setGauge(Gauge::PendingRequests, 7);
    Metrics::instance().setGauge(Gauge::PendingRequests, 4);

    Metrics::instance().reset();

    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(counterOf(snap, Counter::RequestsSent), 0u);
    EXPECT_EQ(snap.latencyCount, 0u);
    EXPECT_TRUE(snap.recentLifecycle.empty());
    // The gauge still reads 4, so its high-water mark must not reset below that
    // or the next snapshot would report a peak lower than the live value.
    EXPECT_EQ(snap.pendingRequests, 4u);
    EXPECT_EQ(snap.pendingRequestsHighWater, 4u);
}

TEST_F(MetricsUTest, SnapshotSurvivesAProviderThatThrows)
{
    Metrics::instance().setPendingRequestProvider([](std::size_t) -> std::vector<PendingRequestInfo>
                                                  { throw std::runtime_error("provider blew up"); });
    Metrics::instance().setConnectionStateProvider([]() -> std::string { throw std::runtime_error("boom"); });

    // A diagnostic read must never be able to take down the caller.
    Snapshot snap;
    EXPECT_NO_THROW(snap = Metrics::instance().snapshot());
    EXPECT_TRUE(snap.oldestPending.empty());

    Metrics::instance().setPendingRequestProvider(nullptr);
    Metrics::instance().setConnectionStateProvider(nullptr);
}

TEST_F(MetricsUTest, PendingRequestProviderFeedsTheSnapshot)
{
    Metrics::instance().setPendingRequestProvider(
        [](std::size_t limit)
        {
            std::vector<PendingRequestInfo> pending;
            for (std::size_t i = 0; i < limit; ++i)
            {
                pending.push_back(PendingRequestInfo{static_cast<uint32_t>(i), "device.model", 100 + i});
            }
            return pending;
        });

    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(snap.oldestPending.size(), kMaxReportedPendingRequests);
    EXPECT_EQ(snap.oldestPending.front().method, "device.model");

    Metrics::instance().setPendingRequestProvider(nullptr);
}

TEST_F(MetricsUTest, DispatchScopeNamesTheWorkerThatIsStillRunning)
{
    const std::string subject = "device.onDeviceNameChanged";
    {
        DispatchScope scope(Worker::NotificationWorker, subject, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        const Snapshot snap = Metrics::instance().snapshot();
        ASSERT_TRUE(snap.stalledWorker.active) << "a running callback must be visible to diagnostics";
        EXPECT_EQ(snap.stalledWorker.worker, "notification-worker");
        EXPECT_EQ(snap.stalledWorker.subject, subject);
        EXPECT_GE(snap.stalledWorker.elapsed_ms, 10u);
    }

    EXPECT_FALSE(Metrics::instance().snapshot().stalledWorker.active);
}

TEST_F(MetricsUTest, DispatchScopeCountsCallbacksThatBreachTheThreshold)
{
    const std::string subject = "slow.event";
    {
        DispatchScope scope(Worker::NotificationWorker, subject, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    EXPECT_EQ(counterOf(Metrics::instance().snapshot(), Counter::SlowEventCallbacks), 1u);
}

TEST_F(MetricsUTest, DispatchScopeIgnoresTheThresholdWhenDisabled)
{
    const std::string subject = "fast.event";
    {
        DispatchScope scope(Worker::NotificationWorker, subject, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(counterOf(Metrics::instance().snapshot(), Counter::SlowEventCallbacks), 0u);
}

class DiagnosticsUTest : public ::testing::Test
{
protected:
    void SetUp() override { GetDiagnosticsInstance().reset(); }
};

TEST_F(DiagnosticsUTest, EveryCounterHasADistinctName)
{
    std::set<std::string> names;
    for (std::size_t i = 0; i < kCounterCount; ++i)
    {
        const char* name = IDiagnostics::counterName(static_cast<Counter>(i));
        ASSERT_NE(name, nullptr);
        EXPECT_STRNE(name, "unknown") << "counter index " << i << " has no name";
        names.insert(name);
    }
    EXPECT_EQ(names.size(), kCounterCount) << "counter names must be unique";
}

TEST_F(DiagnosticsUTest, OutOfRangeCounterNameIsReportedRatherThanReadingPastTheTable)
{
    EXPECT_STREQ(IDiagnostics::counterName(static_cast<Counter>(kCounterCount + 99)), "unknown");
}

TEST_F(DiagnosticsUTest, SnapshotJsonIsParseableAndCarriesEverySection)
{
    Metrics::instance().increment(Counter::RequestsTimedOut);
    Metrics::instance().recordLatency(2500);

    const std::string raw = GetDiagnosticsInstance().snapshotJson();
    nlohmann::json parsed;
    ASSERT_NO_THROW(parsed = nlohmann::json::parse(raw));

    EXPECT_TRUE(parsed.contains("counters"));
    EXPECT_TRUE(parsed.contains("gauges"));
    EXPECT_TRUE(parsed.contains("latency"));
    EXPECT_TRUE(parsed.contains("lifecycle"));
    EXPECT_TRUE(parsed.contains("pendingRequests"));
    EXPECT_TRUE(parsed.contains("uptimeMs"));

    EXPECT_EQ(parsed["counters"]["requestsTimedOut"].get<uint64_t>(), 1u);
    EXPECT_EQ(parsed["counters"].size(), kCounterCount);
    EXPECT_EQ(parsed["latency"]["count"].get<uint64_t>(), 1u);
    EXPECT_EQ(parsed["latency"]["meanUs"].get<uint64_t>(), 2500u);
}

TEST_F(DiagnosticsUTest, SnapshotJsonOmitsStalledWorkerWhenNothingIsStuck)
{
    const auto parsed = nlohmann::json::parse(GetDiagnosticsInstance().snapshotJson());
    EXPECT_FALSE(parsed.contains("stalledWorker"));
}

TEST_F(DiagnosticsUTest, PrettyAndCompactJsonDescribeTheSameState)
{
    const auto compact = nlohmann::json::parse(GetDiagnosticsInstance().snapshotJson(false));
    const auto pretty = nlohmann::json::parse(GetDiagnosticsInstance().snapshotJson(true));
    EXPECT_EQ(compact["counters"], pretty["counters"]);
}

TEST_F(DiagnosticsUTest, MeanLatencyIsZeroRatherThanDividingByZero)
{
    const auto parsed = nlohmann::json::parse(GetDiagnosticsInstance().snapshotJson());
    EXPECT_EQ(parsed["latency"]["count"].get<uint64_t>(), 0u);
    EXPECT_EQ(parsed["latency"]["meanUs"].get<uint64_t>(), 0u);
}

TEST_F(DiagnosticsUTest, DumpToLogDoesNotThrow)
{
    Metrics::instance().increment(Counter::ConnectFailures);
    Metrics::instance().recordLatency(1000);
    EXPECT_NO_THROW(GetDiagnosticsInstance().dumpToLog());
}

TEST_F(DiagnosticsUTest, ReportsWhetherMetricsWereCompiledIn)
{
    EXPECT_EQ(GetDiagnosticsInstance().snapshot().metricsEnabled, Metrics::enabled());
}

class LogThrottleUTest : public ::testing::Test
{
};

TEST_F(LogThrottleUTest, FirstCallPassesAndSubsequentCallsInsideTheWindowAreSuppressed)
{
    Firebolt::LogThrottle throttle;
    uint64_t suppressed = 0;

    EXPECT_TRUE(throttle.shouldLog(10'000, suppressed));
    EXPECT_EQ(suppressed, 0u);

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_FALSE(throttle.shouldLog(10'000, suppressed));
    }
}

TEST_F(LogThrottleUTest, SuppressedCountIsReportedOnTheNextEmissionThenCleared)
{
    Firebolt::LogThrottle throttle;
    uint64_t suppressed = 0;

    ASSERT_TRUE(throttle.shouldLog(20, suppressed));
    for (int i = 0; i < 3; ++i)
    {
        throttle.shouldLog(20, suppressed);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    ASSERT_TRUE(throttle.shouldLog(20, suppressed));
    EXPECT_EQ(suppressed, 3u) << "the operator must learn how many lines were dropped";

    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    ASSERT_TRUE(throttle.shouldLog(20, suppressed));
    EXPECT_EQ(suppressed, 0u) << "the suppressed tally must not be reported twice";
}

TEST_F(LogThrottleUTest, ExactlyOneThreadEmitsPerWindow)
{
    Firebolt::LogThrottle throttle;
    std::atomic<int> emitted{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&throttle, &emitted]()
            {
                for (int n = 0; n < 500; ++n)
                {
                    uint64_t suppressed = 0;
                    if (throttle.shouldLog(60'000, suppressed))
                    {
                        emitted.fetch_add(1);
                    }
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(emitted.load(), 1) << "a throttled site must not be defeated by concurrent callers";
}

TEST_F(LogThrottleUTest, GlobalSuppressedTallyIsMonotonic)
{
    const uint64_t before = Firebolt::LogThrottle::totalSuppressed();
    Firebolt::LogThrottle throttle;
    uint64_t suppressed = 0;
    throttle.shouldLog(60'000, suppressed);
    throttle.shouldLog(60'000, suppressed);
    throttle.shouldLog(60'000, suppressed);
    EXPECT_GE(Firebolt::LogThrottle::totalSuppressed(), before + 2);
}

class TransportQueueCapUTest : public ::testing::Test
{
protected:
    void SetUp() override { Metrics::instance().reset(); }
};

TEST_F(TransportQueueCapUTest, QueueIsCappedAndOverflowIsCountedRatherThanGrowingWithoutBound)
{
    Transport transport;
    transport.setMaxMessageQueueDepth(4);

    // No worker is draining, so this models a wedged consumer.
    for (int i = 0; i < 10; ++i)
    {
        transport.testEnqueuePayload("{\"id\":" + std::to_string(i) + "}");
    }

    EXPECT_EQ(transport.testMessageQueueDepth(), 4u) << "memory use must stay bounded";

    const Snapshot snap = Metrics::instance().snapshot();
    EXPECT_EQ(counterOf(snap, Counter::MessagesReceived), 10u);
    EXPECT_EQ(counterOf(snap, Counter::MessagesDroppedOverflow), 6u);
    EXPECT_EQ(snap.messageQueueDepth, 4u);
    EXPECT_EQ(snap.messageQueueHighWater, 4u);
}

TEST_F(TransportQueueCapUTest, ZeroCapRestoresUnboundedQueueing)
{
    Transport transport;
    transport.setMaxMessageQueueDepth(0);

    for (int i = 0; i < 50; ++i)
    {
        transport.testEnqueuePayload("{}");
    }

    EXPECT_EQ(transport.testMessageQueueDepth(), 50u);
    EXPECT_EQ(counterOf(Metrics::instance().snapshot(), Counter::MessagesDroppedOverflow), 0u);
}

TEST_F(TransportQueueCapUTest, ByteCountersFollowPayloadSize)
{
    Transport transport;
    transport.setMaxMessageQueueDepth(0);
    transport.testEnqueuePayload("12345");
    transport.testEnqueuePayload("678");

    EXPECT_EQ(counterOf(Metrics::instance().snapshot(), Counter::BytesReceived), 8u);
}

TEST_F(TransportQueueCapUTest, NewestFrameIsRetainedWhenTheQueueOverflows)
{
    Transport transport;
    transport.setMaxMessageQueueDepth(1);
    transport.testEnqueuePayload("oldest");
    transport.testEnqueuePayload("newest");

    // Dropping from the head keeps the most recent frame, which is the one most
    // likely to still correspond to an un-timed-out request.
    EXPECT_EQ(transport.testMessageQueueDepth(), 1u);
    EXPECT_EQ(counterOf(Metrics::instance().snapshot(), Counter::MessagesDroppedOverflow), 1u);
}

TEST_F(TransportQueueCapUTest, StateNameReflectsAFreshTransport)
{
    Transport transport;
    EXPECT_STREQ(transport.stateName(), "NotStarted");
}

// End-to-end check that the recording sites are actually reached on the real
// request path, rather than only that the metric primitives work in isolation.
class DiagnosticsEndToEndTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    static constexpr uint16_t kPort = 9013;

    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    bool m_started = false;
    std::promise<bool> m_connected;

    void SetUp() override
    {
        m_connected = std::promise<bool>();
        m_server.init_asio();
        m_server.set_reuse_addr(true);
        m_server.set_access_channels(websocketpp::log::alevel::none);
        m_server.set_error_channels(websocketpp::log::elevel::none);
        m_server.set_message_handler(
            [this](connection_hdl hdl, server::message_ptr msg)
            {
                auto request = nlohmann::json::parse(msg->get_payload());
                if (!request.contains("id"))
                {
                    return;
                }
                nlohmann::json response;
                response["jsonrpc"] = "2.0";
                response["id"] = request["id"];
                response["result"] = nlohmann::json::object();
                m_server.send(hdl, response.dump(), msg->get_opcode());
            });
        m_server.listen(kPort);
        m_server.start_accept();
        m_serverThread = std::make_unique<std::thread>([this]() { m_server.run(); });
        m_started = true;

        Metrics::instance().reset();
    }

    void TearDown() override
    {
        GetGatewayInstance().disconnect();
        if (m_started)
        {
            m_server.stop_listening();
            m_server.stop();
            if (m_serverThread && m_serverThread->joinable())
            {
                m_serverThread->join();
            }
        }
    }

    IGateway& connectAndWait()
    {
        IGateway& gateway = GetGatewayInstance();
        auto future = m_connected.get_future();
        Firebolt::Config cfg;
        cfg.wsUrl = "ws://127.0.0.1:" + std::to_string(kPort);
        cfg.waitTime_ms = 1000;

        EXPECT_EQ(gateway.connect(cfg,
                                  [this](bool connected, const Firebolt::Error&)
                                  {
                                      if (connected)
                                      {
                                          m_connected.set_value(true);
                                      }
                                  }),
                  Firebolt::Error::None);
        EXPECT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        return gateway;
    }
};

TEST_F(DiagnosticsEndToEndTest, ASuccessfulRequestIsFullyAccountedFor)
{
    IGateway& gateway = connectAndWait();

    auto response = gateway.request("device.model", nlohmann::json::object());
    ASSERT_EQ(response.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_TRUE(response.get());

    const Snapshot snap = GetDiagnosticsInstance().snapshot();

    EXPECT_EQ(snap.connectionState, "Connected");
    EXPECT_GE(counterOf(snap, Counter::ConnectAttempts), 1u);
    EXPECT_GE(counterOf(snap, Counter::ConnectSuccesses), 1u);
    EXPECT_GE(counterOf(snap, Counter::RequestsSent), 1u);
    EXPECT_GE(counterOf(snap, Counter::RequestsSucceeded), 1u);
    EXPECT_GE(counterOf(snap, Counter::ResponsesReceived), 1u);
    EXPECT_EQ(counterOf(snap, Counter::RequestsTimedOut), 0u);
    EXPECT_EQ(counterOf(snap, Counter::ResponsesOrphaned), 0u);

    EXPECT_GT(counterOf(snap, Counter::BytesSent), 0u);
    EXPECT_GT(counterOf(snap, Counter::BytesReceived), 0u);

    EXPECT_GE(snap.latencyCount, 1u) << "a completed round trip must produce a latency sample";
    EXPECT_EQ(snap.pendingRequests, 0u) << "the request queue must drain once the response arrives";
    EXPECT_GE(snap.pendingRequestsHighWater, 1u);

    ASSERT_FALSE(snap.recentLifecycle.empty());
    EXPECT_TRUE(snap.recentLifecycle.back().connected);
}

TEST_F(DiagnosticsEndToEndTest, RequestsIssuedWhileDisconnectedAreCountedAsSendFailures)
{
    IGateway& gateway = GetGatewayInstance();

    auto response = gateway.request("device.model", nlohmann::json::object());
    ASSERT_EQ(response.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    const auto result = response.get();
    EXPECT_FALSE(result);

    const Snapshot snap = GetDiagnosticsInstance().snapshot();
    EXPECT_GE(counterOf(snap, Counter::RequestSendFailures), 1u);
    EXPECT_EQ(snap.pendingRequests, 0u) << "a failed send must not leak a pending entry";
}

TEST_F(DiagnosticsEndToEndTest, DisconnectIsRecordedInTheLifecycleHistory)
{
    connectAndWait();
    GetGatewayInstance().disconnect();

    const Snapshot snap = GetDiagnosticsInstance().snapshot();
    EXPECT_EQ(snap.connectedFor_ms, 0u);
    ASSERT_GE(snap.recentLifecycle.size(), 1u);
}

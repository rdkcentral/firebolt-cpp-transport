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
#include "firebolt/logger.h"
#include "utils.h"
#include <atomic>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using namespace Firebolt::Transport;

TEST(GatewayUrlUtilsUTest, VerifyUrls)
{
    bool legacyRPCv1 = false;
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437", legacyRPCv1), "ws://127.0.0.1:3437/?RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437?sessionId=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/?sessionId=abc&RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437/json?session=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/json?session=abc&RPCv2=true");
    EXPECT_EQ(buildGatewayUrl("ws://localhost:9003", legacyRPCv1), "ws://localhost:9003/?RPCv2=true");
}

TEST(GatewayUrlUtilsUTest, VerifyUrls_LegacyMode)
{
    bool legacyRPCv1 = true;
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437", legacyRPCv1), "ws://127.0.0.1:3437/");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437?sessionId=abc", legacyRPCv1), "ws://127.0.0.1:3437/?sessionId=abc");
    EXPECT_EQ(buildGatewayUrl("ws://127.0.0.1:3437/json?session=abc", legacyRPCv1),
              "ws://127.0.0.1:3437/json?session=abc");
    EXPECT_EQ(buildGatewayUrl("ws://localhost:9003", legacyRPCv1), "ws://localhost:9003/");
}

class GatewayUTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    const std::string m_uri = "ws://localhost:9003";
    bool m_serverStarted = false;
    std::map<connection_hdl, nlohmann::json, std::owner_less<connection_hdl>> m_responses;
    std::promise<bool> m_connectionPromise;
    std::function<void(server*, connection_hdl)> m_onMessageAction;
    std::function<void(connection_hdl, server::message_ptr)> m_messageHandler;

    void SetUp() override
    {
        m_connectionPromise = std::promise<bool>();
        m_onMessageAction = nullptr;
        m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
        {
            if (m_onMessageAction)
            {
                m_onMessageAction(&m_server, hdl);
                m_onMessageAction = nullptr;
                return;
            }
            try
            {
                auto request = nlohmann::json::parse(msg->get_payload());
                if (request.contains("id"))
                {
                    nlohmann::json response;
                    if (m_responses.count(hdl))
                    {
                        response = m_responses[hdl];
                        m_responses.erase(hdl);
                    }
                    else
                    {
                        response["jsonrpc"] = "2.0";
                        response["id"] = request["id"];
                        response["result"] = nlohmann::json::object();
                    }
                    m_server.send(hdl, response.dump(), msg->get_opcode());
                }
            }
            catch (const websocketpp::exception& e)
            {
                FAIL() << "Server failed to send message: " << e.what();
            }
            catch (const nlohmann::json::parse_error& e)
            {
                FAIL() << "Failed to parse incoming message: " << e.what();
            }
        };
    }

    void startServer()
    {
        try
        {
            m_server.init_asio();
            m_server.set_reuse_addr(true);

            m_server.set_message_handler(
                [this](connection_hdl hdl, server::message_ptr msg)
                {
                    if (m_messageHandler)
                    {
                        m_messageHandler(hdl, msg);
                    }
                });

            m_server.set_access_channels(websocketpp::log::alevel::none);
            m_server.listen(9003);
            m_server.start_accept();
            m_serverThread = std::make_unique<std::thread>([this]() { m_server.run(); });
            m_serverStarted = true;
        }
        catch (const websocketpp::exception& e)
        {
            FAIL() << "Failed to start websocket server: " << e.what();
        }
    }

    void TearDown() override
    {
        GetGatewayInstance().disconnect();

        if (m_serverStarted)
        {
            m_server.stop_listening();
            m_server.stop();
            if (m_serverThread && m_serverThread->joinable())
            {
                m_serverThread->join();
            }
        }
    }

    Firebolt::Config getTestConfig()
    {
        Firebolt::Config cfg;
        cfg.wsUrl = m_uri;
        cfg.log.level = Firebolt::LogLevel::Debug;
        cfg.waitTime_ms = 1000;
        return cfg;
    }

    void onConnectionChange(bool connected, const Firebolt::Error& /*err*/)
    {
        if (connected)
        {
            try
            {
                m_connectionPromise.set_value(true);
            }
            catch (const std::future_error& e)
            {
                ADD_FAILURE() << "Failed to set connection promise: " << e.what();
            }
        }
    };

    IGateway& connectAndWait()
    {
        startServer();
        IGateway& gateway = GetGatewayInstance();
        auto connectionFuture = m_connectionPromise.get_future();

        Firebolt::Error err = gateway.connect(getTestConfig(), [this](bool connected, const Firebolt::Error& err)
                                              { onConnectionChange(connected, err); });
        EXPECT_EQ(err, Firebolt::Error::None);

        auto status = connectionFuture.wait_for(std::chrono::seconds(2));
        EXPECT_EQ(status, std::future_status::ready) << "Connection timed out";
        if (status == std::future_status::ready)
        {
            EXPECT_TRUE(connectionFuture.get());
        }
        return gateway;
    }
};

TEST_F(GatewayUTest, ConnectAndDisconnect)
{
    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Error err = gateway.connect(getTestConfig(), [this](bool connected, const Firebolt::Error& err)
                                          { onConnectionChange(connected, err); });
    ASSERT_EQ(err, Firebolt::Error::None);

    auto status = connectionFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(status, std::future_status::ready) << "Connection timed out";
    EXPECT_TRUE(connectionFuture.get());

    err = gateway.disconnect();
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayUTest, Request)
{
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_TRUE(result);
}

TEST_F(GatewayUTest, Send)
{
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    Firebolt::Error err = gateway.send("test.method", params);
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayUTest, SubscribeUnsubscribe)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onEvent";
        eventMsg["params"] = {{"fired", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };

    gateway.send("dummy.message", {});

    auto eventStatus = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(eventStatus, std::future_status::ready) << "Event was not received";

    nlohmann::json eventParams = eventFuture.get();
    EXPECT_TRUE(eventParams["fired"].get<bool>());

    err = gateway.unsubscribe("test.onEvent", &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);
}

TEST_F(GatewayUTest, RequestTimeout)
{
    m_messageHandler = [](connection_hdl /*hdl*/, server::message_ptr /*msg*/) {};
    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::milliseconds(2500));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::Timedout);
}

TEST_F(GatewayUTest, RequestWithError)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["error"]["code"] = -32601;
            response["error"]["message"] = "Method not found";
            m_server.send(hdl, response.dump(), msg->get_opcode());
        }
        catch (...)
        {
            FAIL() << "Server failed to send error response";
        }
    };

    IGateway& gateway = connectAndWait();

    nlohmann::json params = {{"key", "value"}};
    auto responseFuture = gateway.request("test.method", params);

    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready) << "Request timed out";

    auto result = responseFuture.get();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), Firebolt::Error::MethodNotFound);
    EXPECT_EQ(result.errorInfo().error(), -32601);
    EXPECT_EQ(result.errorInfo().message(), "Method not found");
}

TEST_F(GatewayUTest, MultipleSubscriptions)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise1;
    auto eventFuture1 = eventPromise1.get_future();
    auto onEvent1 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    std::promise<nlohmann::json> eventPromise2;
    auto eventFuture2 = eventPromise2.get_future();
    auto onEvent2 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    gateway.subscribe("test.onEvent", onEvent1, &eventPromise1);
    gateway.subscribe("test.onEvent", onEvent2, &eventPromise2);

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onEvent";
        eventMsg["params"] = {{"fired", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy.message", {});

    ASSERT_EQ(eventFuture1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_EQ(eventFuture2.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    EXPECT_TRUE(eventFuture1.get()["fired"].get<bool>());
    EXPECT_TRUE(eventFuture2.get()["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise1);
    gateway.unsubscribe("test.onEvent", &eventPromise2);
}

TEST_F(GatewayUTest, UnsubscribeSpecific)
{
    IGateway& gateway = connectAndWait();

    std::promise<nlohmann::json> eventPromise1;
    auto eventFuture1 = eventPromise1.get_future();
    auto onEvent1 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    std::promise<nlohmann::json> eventPromise2;
    auto eventFuture2 = eventPromise2.get_future();
    auto onEvent2 = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    gateway.subscribe("test.onEvent", onEvent1, &eventPromise1);
    gateway.subscribe("test.onEvent", onEvent2, &eventPromise2);

    gateway.unsubscribe("test.onEvent", &eventPromise1);

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json eventMsg;
        eventMsg["jsonrpc"] = "2.0";
        eventMsg["method"] = "test.onEvent";
        eventMsg["params"] = {{"fired", true}};
        s->send(hdl, eventMsg.dump(), websocketpp::frame::opcode::text);
    };
    gateway.send("dummy.message", {});

    EXPECT_EQ(eventFuture1.wait_for(std::chrono::seconds(2)), std::future_status::timeout);
    ASSERT_EQ(eventFuture2.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(eventFuture2.get()["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise2);
}

TEST_F(GatewayUTest, InvalidNotification)
{
    IGateway& gateway = connectAndWait();

    m_onMessageAction = [](server* s, connection_hdl hdl)
    {
        nlohmann::json invalidMsg1;
        invalidMsg1["jsonrpc"] = "2.0";
        invalidMsg1["method"] = "test.onEvent";
        s->send(hdl, invalidMsg1.dump(), websocketpp::frame::opcode::text);

        nlohmann::json invalidMsg2;
        invalidMsg2["jsonrpc"] = "2.0";
        invalidMsg2["method"] = "test.onEvent";
        invalidMsg2["id"] = 123;
        invalidMsg2["params"] = {};
        s->send(hdl, invalidMsg2.dump(), websocketpp::frame::opcode::text);

        nlohmann::json invalidMsg3;
        invalidMsg3["jsonrpc"] = "2.0";
        s->send(hdl, invalidMsg3.dump(), websocketpp::frame::opcode::text);
    };

    gateway.send("dummy.message", {});

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto responseFuture = gateway.request("test.method", {{"key", "value"}});
    auto responseStatus = responseFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(responseStatus, std::future_status::ready)
        << "Gateway did not respond to a valid request after invalid notifications.";

    auto result = responseFuture.get();
    EXPECT_TRUE(result) << "Gateway returned an error for a valid request after invalid notifications.";
}

TEST_F(GatewayUTest, LegacyRPCv1Event)
{
    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        if (request["method"].get<std::string>().find(".onEvent") != std::string::npos)
        {
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["result"]["listening"] = true;
            m_server.send(hdl, response.dump(), msg->get_opcode());

            nlohmann::json legacyEvent;
            legacyEvent["jsonrpc"] = "2.0";
            legacyEvent["id"] = request["id"];
            legacyEvent["result"] = {{"fired", true}};
            m_server.send(hdl, legacyEvent.dump(), msg->get_opcode());
        }
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);

    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto eventStatus = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(eventStatus, std::future_status::ready) << "Legacy event was not received";

    nlohmann::json eventParams = eventFuture.get();
    EXPECT_TRUE(eventParams["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise);
}

TEST_F(GatewayUTest, LegacyRPCv1Event_array)
{
    std::promise<nlohmann::json> eventPromise;
    auto eventFuture = eventPromise.get_future();

    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        if (request["method"].get<std::string>().find(".onEvent") != std::string::npos)
        {
            nlohmann::json response;
            response["jsonrpc"] = "2.0";
            response["id"] = request["id"];
            response["result"]["listening"] = true;
            m_server.send(hdl, response.dump(), msg->get_opcode());

            nlohmann::json legacyEvent;
            legacyEvent["jsonrpc"] = "2.0";
            legacyEvent["id"] = request["id"];
            legacyEvent["result"] = {{{"fired", true}}};
            m_server.send(hdl, legacyEvent.dump(), msg->get_opcode());
        }
    };

    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Config cfg = getTestConfig();
    cfg.legacyRPCv1 = true;
    Firebolt::Error connectErr = gateway.connect(cfg, [this](bool connected, const Firebolt::Error& err)
                                                 { onConnectionChange(connected, err); });
    ASSERT_EQ(connectErr, Firebolt::Error::None);

    ASSERT_EQ(connectionFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &eventPromise);
    EXPECT_EQ(err, Firebolt::Error::None);

    auto eventStatus = eventFuture.wait_for(std::chrono::seconds(2));
    ASSERT_EQ(eventStatus, std::future_status::ready) << "Legacy event was not received";

    nlohmann::json eventParams = eventFuture.get();
    EXPECT_TRUE(eventParams[0]["fired"].get<bool>());

    gateway.unsubscribe("test.onEvent", &eventPromise);
}

TEST_F(GatewayUTest, UnsubscribeFromCallbackDoesNotDeadlock)
{
    m_messageHandler = [this](connection_hdl hdl, server::message_ptr msg)
    {
        try
        {
            auto request = nlohmann::json::parse(msg->get_payload());
            nlohmann::json response;
            if (request.contains("id") && request.contains("params") && request["params"].contains("listen"))
            {
                response["jsonrpc"] = "2.0";
                response["id"] = request["id"];
                response["result"] = {{"listening", request["params"]["listen"]}};
            }
            if (request.contains("id") && request.contains("method") && request["method"] == "fire.event")
            {
                response["jsonrpc"] = "2.0";
                response["method"] = "test.onEvent";
                response["params"] = {{"fired", true}};
            }
            if (!response.empty())
            {
                m_server.send(hdl, response.dump(), websocketpp::frame::opcode::text);
            }
        }
        catch (...)
        {
        }
    };

    IGateway& gateway = connectAndWait();

    struct CallbackCtx
    {
        IGateway* gateway;
        std::promise<void>* done;
        std::string eventName;
    };

    std::promise<void> donePromise;
    auto doneFuture = donePromise.get_future();

    CallbackCtx ctx{&gateway, &donePromise, "test.onEvent"};

    auto onEvent = [](void* usercb, const nlohmann::json& /*params*/)
    {
        auto* ctx = static_cast<CallbackCtx*>(usercb);
        ctx->gateway->unsubscribe(ctx->eventName, usercb);
        try
        {
            ctx->done->set_value();
        }
        catch (...)
        {
        }
    };

    Firebolt::Error err = gateway.subscribe("test.onEvent", onEvent, &ctx);
    ASSERT_EQ(err, Firebolt::Error::None);

    gateway.send("fire.event", {});

    auto status = doneFuture.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(status, std::future_status::ready) << "Callback blocked (possible deadlock)";
}

// ---------------------------------------------------------------------------
// Regression test: disconnect() must return quickly even when the server dies
// while active subscriptions are held.
//
// Repro scenario (RDKEMW-16573):
//   1. Client subscribes to an event (gateway sends subscribe ACK)
//   2. Server disappears abruptly (no WS close handshake, no unsubscribe ACK)
//   3. Client calls disconnect()
//
// Before the fix: disconnect() blocked for ~waitTime_ms per subscription
// (request(...).get() inside unsubscribe() held the calling thread).
//
// After the fix: disconnect() uses wait_for(50ms) ceilings and returns in
// well under 500ms regardless of server responsiveness.
//
// The test asserts disconnect() completes within 500ms (generous allowance).
// On an unpatched build it will take >= waitTime_ms (1000ms default) and fail.
// ---------------------------------------------------------------------------
TEST_F(GatewayUTest, DisconnectDoesNotHangWhenServerDisappearsWithActiveSubscription)
{
    // Use a silent message handler so subscribe ACK is sent but unsubscribe ACK
    // is intentionally never sent (simulates server dying after the subscription).
    bool subscribeAckSent = false;
    m_messageHandler = [this, &subscribeAckSent](connection_hdl hdl, server::message_ptr msg)
    {
        auto request = nlohmann::json::parse(msg->get_payload());
        const std::string method = request.value("method", "");

        // Ack the subscribe so the client considers it active.
        if (!subscribeAckSent && method.find(".on") != std::string::npos)
        {
            nlohmann::json ack;
            ack["jsonrpc"] = "2.0";
            ack["id"] = request["id"];
            ack["result"] = {{"listening", true}};
            m_server.send(hdl, ack.dump(), msg->get_opcode());
            subscribeAckSent = true;
            return;
        }
        // All subsequent messages (including the unsubscribe request) get no reply.
        // This simulates a dead server.
    };

    // Connect with a callback that mirrors the Sky app's log output.
    // On device these appear as:
    //   [Firebolt] FireboltService uninitialize
    //   [Firebolt] Connection state changed: Disconnected, error=2
    //   [Firebolt] Connection state changed: Disconnected end
    startServer();
    IGateway& gateway = GetGatewayInstance();
    auto connectionFuture = m_connectionPromise.get_future();

    Firebolt::Error err =
        gateway.connect(getTestConfig(),
                        [this](bool connected, const Firebolt::Error& error)
                        {
                            if (connected)
                            {
                                onConnectionChange(connected, error);
                            }
                            else
                            {
                                FIREBOLT_LOG_INFO("FireboltApp", "Connection state changed: Disconnected, error=%d",
                                                  static_cast<int>(error));
                                FIREBOLT_LOG_INFO("FireboltApp", "Connection state changed: Disconnected end");
                            }
                        });
    ASSERT_EQ(err, Firebolt::Error::None);
    connectionFuture.wait_for(std::chrono::seconds(2));

    std::promise<nlohmann::json> eventPromise;
    auto onEvent = [](void* usercb, const nlohmann::json& params)
    { static_cast<std::promise<nlohmann::json>*>(usercb)->set_value(params); };

    Firebolt::Error subErr = gateway.subscribe("test.onStateChanged", onEvent, &eventPromise);
    ASSERT_EQ(subErr, Firebolt::Error::None) << "subscribe() failed";

    // Give the subscribe ACK time to arrive.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Switch to a silent handler: server keeps the TCP connection open but
    // never replies to anything (including unsubscribe ACKs).
    // This is the on-device scenario: WPEFramework plugin stops responding
    // without closing the socket.  The bug is in unsubscribe(): pre-fix calls
    // request().get() which blocks indefinitely waiting for the ACK.
    m_messageHandler = [](connection_hdl, server::message_ptr) { /* drop everything */ };

    // Time unsubscribe() + disconnect() together.
    // The hang is in unsubscribe(): pre-fix calls request().get() which blocks
    // indefinitely waiting for the server's unsubscribe ACK (which never comes).
    // With the bug: unsubscribe() blocks >= waitTime_ms (1000 ms).
    // With the fix: unsubscribe() returns in <= 50 ms (wait_for ceiling).
    FIREBOLT_LOG_INFO("FireboltApp", "FireboltService uninitialize");
    auto t0 = std::chrono::steady_clock::now();
    gateway.unsubscribe("test.onStateChanged", &eventPromise);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);

    std::cout << "[timing] unsubscribe() took " << elapsed.count() << " ms\n";

    gateway.disconnect();

    EXPECT_LT(elapsed.count(), 200) << "unsubscribe() took " << elapsed.count()
                                    << " ms — blocked waiting for ACK from silent server (bug reproduced)";
}

// ---------------------------------------------------------------------------
// Retry tests — verify reconnect_max_attempts / reconnect_delay_ms behaviour.
// Uses port 9008 to avoid conflict with GatewayUTest (9003).
// ---------------------------------------------------------------------------

class GatewayRetryUTest : public ::testing::Test
{
protected:
    using server = websocketpp::server<websocketpp::config::asio>;
    using connection_hdl = websocketpp::connection_hdl;

    server m_server;
    std::unique_ptr<std::thread> m_serverThread;
    bool m_serverStarted = false;
    const std::string m_uri = "ws://127.0.0.1:9008";

    void startServer()
    {
        try
        {
            m_server.init_asio();
            m_server.set_reuse_addr(true);
            m_server.clear_access_channels(websocketpp::log::alevel::all);
            m_server.listen(
                websocketpp::lib::asio::ip::tcp::endpoint(websocketpp::lib::asio::ip::address::from_string("127.0.0.1"),
                                                          9008));
            m_server.start_accept();
            m_serverThread = std::make_unique<std::thread>([this]() { m_server.run(); });
            m_serverStarted = true;
        }
        catch (const websocketpp::exception& ex)
        {
            FAIL() << "GatewayRetryUTest: server startup failed: " << ex.what();
        }
    }

    void TearDown() override
    {
        GetGatewayInstance().disconnect();
        if (m_serverStarted)
        {
            m_server.stop_listening();
            m_server.stop();
            if (m_serverThread && m_serverThread->joinable())
                m_serverThread->join();
        }
    }

    Firebolt::Config retryConfig(unsigned maxAttempts, unsigned delayMs = 50)
    {
        Firebolt::Config cfg{};
        cfg.wsUrl = m_uri;
        cfg.reconnect_max_attempts = maxAttempts;
        cfg.reconnect_delay_ms = delayMs;
        return cfg;
    }
};

// Server starts listening only after 2 retry delays have elapsed.
// connect() should keep retrying and ultimately return Error::None.
TEST_F(GatewayRetryUTest, RetryConnectsWhenServerDelayed)
{
    std::thread serverStarter(
        [this]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            startServer();
        });

    IGateway& gateway = GetGatewayInstance();
    std::promise<bool> connectedPromise;
    auto connectedFuture = connectedPromise.get_future();
    std::atomic<bool> promiseSet{false};

    Firebolt::Error err = gateway.connect(retryConfig(5, 50),
                                          [&](bool connected, const Firebolt::Error& /*error*/)
                                          {
                                              if (!promiseSet.exchange(true))
                                                  connectedPromise.set_value(connected);
                                          });

    serverStarter.join();

    ASSERT_EQ(err, Firebolt::Error::None) << "connect() should succeed after retries";
    ASSERT_EQ(connectedFuture.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_TRUE(connectedFuture.get());
    // Disconnect before locals go out of scope so TearDown's disconnect() is a
    // no-op and cannot invoke this callback after the frame is gone.
    gateway.disconnect();
}

// connect() with reconnect_max_attempts=0 (no retries) should return
// NotConnected immediately when the server is not listening.
TEST_F(GatewayRetryUTest, NoRetryFailsFast)
{
    IGateway& gateway = GetGatewayInstance();
    std::atomic<int> callbackCount{0};

    auto t0 = std::chrono::steady_clock::now();
    Firebolt::Error err =
        gateway.connect(retryConfig(0), [&](bool /*connected*/, const Firebolt::Error& /*error*/) { ++callbackCount; });
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);

    EXPECT_NE(err, Firebolt::Error::None) << "connect() should fail when no server is listening";
    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_LT(elapsed.count(), 5000) << "No-retry connect should fail quickly";
}

// disconnect() called from another thread while the retry loop is sleeping
// between attempts.  connect() must return promptly (not after the full delay).
TEST_F(GatewayRetryUTest, DisconnectAbortsRetryDelay)
{
    IGateway& gateway = GetGatewayInstance();

    // 3 attempts with 500 ms delay — without the abort fix this would take ~1500 ms.
    auto cfg = retryConfig(3, 500);

    std::thread disconnecter(
        [&gateway]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            gateway.disconnect();
        });

    auto t0 = std::chrono::steady_clock::now();
    gateway.connect(cfg, [](bool, const Firebolt::Error&) {});
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);

    disconnecter.join();
    EXPECT_LT(elapsed.count(), 800) << "connect() took " << elapsed.count()
                                    << " ms — retry delay was not aborted by disconnect()";
}

// A second connect() while already connected must return AlreadyConnected and
// must NOT invoke the new callback with (false, ...) — doing so would be a
// spurious "disconnected" event to the caller.
TEST_F(GatewayRetryUTest, AlreadyConnectedNoFalseDisconnect)
{
    startServer();
    IGateway& gateway = GetGatewayInstance();

    // First connect — must succeed.
    std::atomic<int> firstCallbacks{0};
    Firebolt::Error firstErr = gateway.connect(retryConfig(0), [&](bool, const Firebolt::Error&) { ++firstCallbacks; });
    ASSERT_EQ(firstErr, Firebolt::Error::None) << "first connect() should succeed";

    // Second connect() while already connected.
    std::atomic<int> falseDisconnects{0};
    Firebolt::Error secondErr = gateway.connect(retryConfig(0),
                                                [&](bool connected, const Firebolt::Error&)
                                                {
                                                    if (!connected)
                                                        ++falseDisconnects;
                                                });

    EXPECT_EQ(secondErr, Firebolt::Error::AlreadyConnected);
    EXPECT_EQ(falseDisconnects.load(), 0) << "second connect() must not emit a false disconnect event";
    // Disconnect before locals go out of scope (same reasoning as RetryConnectsWhenServerDelayed).
    gateway.disconnect();
}
